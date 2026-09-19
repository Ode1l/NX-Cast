#include "remote_video.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex AirPlayRemoteMutex;
#else
#include <pthread.h>
typedef pthread_mutex_t AirPlayRemoteMutex;
#endif

#include "protocol/airplay/protocol/plist.h"
#include "protocol/airplay/media/remote_hls.h"
#include "protocol/airplay/trace.h"

#define AIRPLAY_REMOTE_TEXT_BODY_MAX (16u * 1024u)
#define AIRPLAY_REMOTE_XML_MAX 2048u

typedef struct
{
    char url[AIRPLAY_REMOTE_VIDEO_URL_MAX + 1u];
    char metadata[AIRPLAY_REMOTE_VIDEO_METADATA_MAX + 1u];
    double start_value;
    bool start_is_fraction;
    bool has_start;
} AirPlayRemotePlayRequest;

typedef enum
{
    AIRPLAY_REMOTE_VIDEO_SESSION_IDLE = 0,
    AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS,
    AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE
} AirPlayRemoteVideoSessionState;

struct AirPlayRemoteVideo
{
    AirPlayRemoteVideoOps ops;
    AirPlayRemoteMutex mutex;
    uint64_t owner_session_id;
    uint32_t generation;
    int pending_seek_ms;
    int pending_scrub_ms;
    double pending_fraction;
    double requested_rate;
    bool pending_seek;
    bool pending_fraction_seek;
    bool pending_scrub;
    AirPlayRemoteVideoSessionState state;
    bool owner_claimed;
    bool control_attached;
    bool mutex_ready;
    char pending_metadata[AIRPLAY_REMOTE_VIDEO_METADATA_MAX + 1u];
    AirPlayRemoteHls *hls;
};

static bool remote_mutex_init(AirPlayRemoteMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void remote_mutex_destroy(AirPlayRemoteMutex *mutex)
{
#ifndef __SWITCH__
    pthread_mutex_destroy(mutex);
#else
    (void)mutex;
#endif
}

static void remote_mutex_lock(AirPlayRemoteMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void remote_mutex_unlock(AirPlayRemoteMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

static bool content_type_is(const AirPlayRtspRequest *request,
                            const char *expected)
{
    const char *value = airplay_rtsp_request_header(request, "Content-Type");
    size_t size = strlen(expected);

    return value && strncasecmp(value, expected, size) == 0 &&
           (value[size] == '\0' || value[size] == ';');
}

static bool copy_bounded(char *output, size_t output_size, const char *input,
                         size_t input_size)
{
    if (!output || output_size == 0u || !input || input_size >= output_size)
        return false;
    memcpy(output, input, input_size);
    output[input_size] = '\0';
    return true;
}

bool airplay_remote_video_url_supported(const char *url)
{
    const char *authority;
    const char *end;
    size_t size;

    if (!url)
        return false;
    size = strlen(url);
    if (size == 0u || size > AIRPLAY_REMOTE_VIDEO_URL_MAX)
        return false;
    if (strncasecmp(url, "http://", 7u) == 0)
        authority = url + 7u;
    else if (strncasecmp(url, "https://", 8u) == 0)
        authority = url + 8u;
    else
        return false;
    end = authority;
    while (*end && *end != '/' && *end != '?' && *end != '#')
    {
        unsigned char character = (unsigned char)*end;

        if (character <= 0x20u || character == 0x7fu || character == '@')
            return false;
        ++end;
    }
    if (end == authority)
        return false;
    for (const char *cursor = end; *cursor; ++cursor)
    {
        unsigned char character = (unsigned char)*cursor;

        if (character < 0x20u || character == 0x7fu)
            return false;
    }
    return true;
}

static bool parse_double_value(const char *value, double maximum,
                               double *value_out)
{
    char *end = NULL;
    double parsed;

    if (!value || !value_out)
        return false;
    parsed = strtod(value, &end);
    while (end && *end && isspace((unsigned char)*end))
        ++end;
    if (!end || end == value || *end != '\0' || !isfinite(parsed) ||
        parsed < 0.0 || parsed > maximum)
        return false;
    *value_out = parsed;
    return true;
}

static bool set_text_parameter(AirPlayRemotePlayRequest *play,
                               const char *name, size_t name_size,
                               const char *value, size_t value_size)
{
    char number[64];

    while (name_size && isspace((unsigned char)name[0]))
    {
        ++name;
        --name_size;
    }
    while (name_size && isspace((unsigned char)name[name_size - 1u]))
        --name_size;
    while (value_size && isspace((unsigned char)value[0]))
    {
        ++value;
        --value_size;
    }
    while (value_size && isspace((unsigned char)value[value_size - 1u]))
        --value_size;
    if (name_size == strlen("Content-Location") &&
        strncasecmp(name, "Content-Location", name_size) == 0)
        return copy_bounded(play->url, sizeof(play->url), value, value_size);
    if ((name_size == strlen("Start-Position") &&
         strncasecmp(name, "Start-Position", name_size) == 0) ||
        (name_size == strlen("Start-Position-Seconds") &&
         strncasecmp(name, "Start-Position-Seconds", name_size) == 0))
    {
        if (!copy_bounded(number, sizeof(number), value, value_size) ||
            !parse_double_value(number, (double)INT_MAX / 1000.0,
                                &play->start_value))
            return false;
        play->start_is_fraction = name_size == strlen("Start-Position");
        if (play->start_is_fraction && play->start_value > 1.0)
            return false;
        play->has_start = true;
    }
    return true;
}

static bool parse_text_play(const AirPlayRtspRequest *request,
                            AirPlayRemotePlayRequest *play)
{
    const char *location = airplay_rtsp_request_header(request,
                                                       "Content-Location");
    const char *start = airplay_rtsp_request_header(request, "Start-Position");
    size_t offset = 0u;

    if (request->body_length > AIRPLAY_REMOTE_TEXT_BODY_MAX)
        return false;
    if (location && !copy_bounded(play->url, sizeof(play->url), location,
                                  strlen(location)))
        return false;
    if (start && !set_text_parameter(play, "Start-Position",
                                     strlen("Start-Position"), start,
                                     strlen(start)))
        return false;
    while (offset < request->body_length)
    {
        size_t line_start = offset;
        size_t line_end;
        size_t colon;

        while (offset < request->body_length && request->body[offset] != '\n')
            ++offset;
        line_end = offset;
        if (offset < request->body_length)
            ++offset;
        if (line_end > line_start && request->body[line_end - 1u] == '\r')
            --line_end;
        if (line_end == line_start)
            continue;
        colon = line_start;
        while (colon < line_end && request->body[colon] != ':')
            ++colon;
        if (colon == line_end ||
            !set_text_parameter(play, (const char *)request->body + line_start,
                                colon - line_start,
                                (const char *)request->body + colon + 1u,
                                line_end - colon - 1u))
            return false;
    }
    return play->url[0] != '\0';
}

static bool plist_number(const AirPlayPlistValue *value, double *number_out)
{
    uint64_t integer;

    if (airplay_plist_get_real(value, number_out))
        return true;
    if (airplay_plist_get_uint(value, &integer) && integer <= INT_MAX)
    {
        *number_out = (double)integer;
        return true;
    }
    return false;
}

static bool parse_binary_play(const AirPlayRtspRequest *request,
                              AirPlayRemotePlayRequest *play)
{
    static const char *const metadata_keys[] = {
        "title",
        "Content-Title",
        "name",
        "clientProcName",
    };
    AirPlayPlistValue *root = NULL;
    const AirPlayPlistValue *value;
    AirPlayPlistError error;
    const char *text;
    size_t key_index;
    bool ok = false;

    if (!airplay_plist_decode(request->body, request->body_length, &root,
                              &error) ||
        airplay_plist_type(root) != AIRPLAY_PLIST_TYPE_DICT)
        goto cleanup;
    value = airplay_plist_dict_get(root, "Content-Location");
    text = airplay_plist_get_string(value);
    if (!text || !copy_bounded(play->url, sizeof(play->url), text,
                               strlen(text)))
        goto cleanup;
    for (key_index = 0u;
         key_index < sizeof(metadata_keys) / sizeof(metadata_keys[0]);
         ++key_index)
    {
        value = airplay_plist_dict_get(root, metadata_keys[key_index]);
        if (!value)
            continue;
        text = airplay_plist_get_string(value);
        if (!text || !copy_bounded(play->metadata, sizeof(play->metadata),
                                   text, strlen(text)))
            goto cleanup;
        if (play->metadata[0] != '\0')
            break;
    }
    value = airplay_plist_dict_get(root, "Start-Position-Seconds");
    if (value)
    {
        if (!plist_number(value, &play->start_value) ||
            !isfinite(play->start_value) || play->start_value < 0.0 ||
            play->start_value > (double)INT_MAX / 1000.0)
            goto cleanup;
        play->has_start = true;
    }
    else
    {
        value = airplay_plist_dict_get(root, "Start-Position");
        if (value)
        {
            if (!plist_number(value, &play->start_value) ||
                !isfinite(play->start_value) || play->start_value < 0.0 ||
                play->start_value > 1.0)
                goto cleanup;
            play->start_is_fraction = true;
            play->has_start = true;
        }
    }
    ok = true;

cleanup:
    airplay_plist_free(root);
    return ok;
}

static bool parse_play_request(const AirPlayRtspRequest *request,
                               AirPlayRemotePlayRequest *play)
{
    bool binary;

    memset(play, 0, sizeof(*play));
    binary = content_type_is(request, "application/x-apple-binary-plist") ||
             (request->body_length >= 8u &&
              memcmp(request->body, "bplist00", 8u) == 0);
    if (!(binary ? parse_binary_play(request, play) :
                   parse_text_play(request, play)))
        return false;
    return airplay_remote_video_url_supported(play->url) ||
           airplay_remote_hls_locator_supported(play->url);
}

static bool query_number(const char *uri, const char *name, double maximum,
                         double *value_out)
{
    const char *query = strchr(uri, '?');
    size_t name_size = strlen(name);

    if (!query)
        return false;
    ++query;
    while (*query)
    {
        const char *equals = strchr(query, '=');
        const char *end = strchr(query, '&');
        char number[64];
        size_t value_size;

        if (!end)
            end = query + strlen(query);
        if (!equals || equals >= end)
            return false;
        if ((size_t)(equals - query) == name_size &&
            strncasecmp(query, name, name_size) == 0)
        {
            value_size = (size_t)(end - equals - 1u);
            return copy_bounded(number, sizeof(number), equals + 1u,
                                value_size) &&
                   parse_double_value(number, maximum, value_out);
        }
        query = *end ? end + 1u : end;
    }
    return false;
}

static bool session_owned(AirPlayRemoteVideo *remote, uint64_t session_id)
{
    bool owned;

    remote_mutex_lock(&remote->mutex);
    owned = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
            remote->owner_session_id == session_id;
    remote_mutex_unlock(&remote->mutex);
    return owned;
}

static bool session_hls_pending(AirPlayRemoteVideo *remote,
                                uint64_t session_id)
{
    bool pending;

    remote_mutex_lock(&remote->mutex);
    pending = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
              remote->owner_session_id == session_id;
    remote_mutex_unlock(&remote->mutex);
    return pending;
}

static bool session_bound(AirPlayRemoteVideo *remote, uint64_t session_id)
{
    bool bound;

    remote_mutex_lock(&remote->mutex);
    bound = remote->state != AIRPLAY_REMOTE_VIDEO_SESSION_IDLE &&
            remote->owner_session_id == session_id;
    remote_mutex_unlock(&remote->mutex);
    return bound;
}

static void clear_owner_locked(AirPlayRemoteVideo *remote)
{
    remote->owner_session_id = 0u;
    remote->pending_seek_ms = 0;
    remote->pending_fraction = 0.0;
    remote->requested_rate = 0.0;
    remote->pending_seek = false;
    remote->pending_fraction_seek = false;
    remote->pending_scrub_ms = 0;
    remote->pending_scrub = false;
    remote->pending_metadata[0] = '\0';
    remote->state = AIRPLAY_REMOTE_VIDEO_SESSION_IDLE;
    remote->owner_claimed = false;
    remote->control_attached = false;
}

static bool send_hls_event(AirPlayRemoteVideo *remote, uint64_t session_id,
                           const AirPlayRemoteHlsEvent *event)
{
    AirPlayRtspHeader headers[2] = {0};
    AirPlayRtspOutboundRequest outbound = {
        .method = "POST",
        .uri = "/event",
        .protocol = "HTTP/1.1",
        .headers = headers,
        .header_count = 2u,
        .body = event ? event->body : NULL,
        .body_length = event ? event->body_length : 0u,
    };

    if (!remote || !event || !remote->ops.send_reverse_request ||
        event->body_length == 0u)
        return false;
    snprintf(headers[0].name, sizeof(headers[0].name),
             "X-Apple-Session-ID");
    snprintf(headers[0].value, sizeof(headers[0].value), "%s",
             event->apple_session_id);
    snprintf(headers[1].name, sizeof(headers[1].name), "Content-Type");
    snprintf(headers[1].value, sizeof(headers[1].value),
             "text/x-apple-plist+xml");
    return remote->ops.send_reverse_request(session_id, &outbound,
                                             remote->ops.user_data);
}

static void fail_generation(AirPlayRemoteVideo *remote, uint64_t session_id,
                            uint32_t generation)
{
    bool stop = false;
    bool release = false;

    remote_mutex_lock(&remote->mutex);
    if (remote->state != AIRPLAY_REMOTE_VIDEO_SESSION_IDLE &&
        remote->owner_session_id == session_id &&
        remote->generation == generation)
    {
        stop = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE;
        release = remote->owner_claimed;
        clear_owner_locked(remote);
        remote->generation++;
    }
    remote_mutex_unlock(&remote->mutex);
    airplay_remote_hls_reset(remote->hls, session_id, generation);
    if (stop)
        (void)remote->ops.stop(remote->ops.user_data);
    if (release && remote->ops.release_owner)
        remote->ops.release_owner(session_id, remote->ops.user_data);
}

static bool handle_play(AirPlayRemoteVideo *remote, uint64_t session_id,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    AirPlayRemotePlayRequest play;
    AirPlayRemoteHlsEvent event = {0};
    const char *apple_session_id;
    uint32_t generation;
    uint16_t control_port = 0u;
    bool reverse_hls;
    bool accepted = false;
    bool reuse_owner = false;
    bool replace_owner = false;
    bool previous_active = false;
    bool previous_claimed = false;
    uint64_t previous_session_id = 0u;
    uint32_t previous_generation = 0u;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!parse_play_request(request, &play))
        return airplay_rtsp_response_set_status(response, 400);
    reverse_hls = airplay_remote_hls_locator_supported(play.url);
    apple_session_id = airplay_rtsp_request_header(request,
                                                    "X-Apple-Session-ID");
    if (reverse_hls)
    {
        if (!apple_session_id || !remote->ops.send_reverse_request ||
            !remote->ops.control_port ||
            (control_port = remote->ops.control_port(
                 remote->ops.user_data)) == 0u)
            return airplay_rtsp_response_set_status(response, 503);
    }
    remote_mutex_lock(&remote->mutex);
    reuse_owner = !reverse_hls &&
                  remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
                  remote->owner_session_id == session_id &&
                  remote->owner_claimed;
    replace_owner = remote->state != AIRPLAY_REMOTE_VIDEO_SESSION_IDLE &&
                    !reuse_owner;
    if (replace_owner)
    {
        previous_active =
            remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE;
        previous_claimed = remote->owner_claimed;
        previous_session_id = remote->owner_session_id;
        previous_generation = remote->generation;
    }
    remote->state = reverse_hls ? AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS
                                : AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE;
    remote->owner_claimed = reuse_owner;
    remote->owner_session_id = session_id;
    remote->generation++;
    generation = remote->generation;
    remote->requested_rate = 1.0;
    remote->pending_seek = play.has_start && !play.start_is_fraction &&
                           play.start_value > 0.0;
    remote->pending_seek_ms = remote->pending_seek
                                  ? (int)(play.start_value * 1000.0 + 0.5)
                                  : 0;
    remote->pending_fraction_seek = play.has_start && play.start_is_fraction &&
                                    play.start_value > 0.0;
    remote->pending_fraction = remote->pending_fraction_seek
                                   ? play.start_value
                                   : 0.0;
    snprintf(remote->pending_metadata, sizeof(remote->pending_metadata), "%s",
             play.metadata);
    remote_mutex_unlock(&remote->mutex);

    if (replace_owner)
    {
        airplay_remote_hls_reset(remote->hls, previous_session_id,
                                 previous_generation);
        if (previous_active)
            (void)remote->ops.stop(remote->ops.user_data);
        if (previous_claimed && remote->ops.release_owner)
            remote->ops.release_owner(previous_session_id,
                                      remote->ops.user_data);
    }
    airplay_remote_hls_reset(remote->hls, 0u, 0u);

    if (reverse_hls)
    {
        accepted = airplay_remote_hls_begin(
                       remote->hls, session_id, generation, control_port,
                       apple_session_id, play.url, &event) &&
                   send_hls_event(remote, session_id, &event);
        airplay_remote_hls_event_clear(&event);
    }
    else
    {
        const bool claim_available = remote->ops.claim_owner != NULL;
        bool claim_generation;
        bool claimed = false;

        if (!reuse_owner && claim_available &&
            !remote->ops.claim_owner(session_id, remote->ops.user_data))
        {
            remote_mutex_lock(&remote->mutex);
            if (remote->generation == generation &&
                remote->owner_session_id == session_id)
                clear_owner_locked(remote);
            remote_mutex_unlock(&remote->mutex);
            return airplay_rtsp_response_set_status(response, 409);
        }
        remote_mutex_lock(&remote->mutex);
        claim_generation =
            remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
            remote->owner_session_id == session_id &&
            remote->generation == generation;
        if (!claim_generation)
        {
            remote_mutex_unlock(&remote->mutex);
            if (claim_available && remote->ops.release_owner)
                remote->ops.release_owner(session_id, remote->ops.user_data);
            return airplay_rtsp_response_set_status(response, 409);
        }
        if (!reuse_owner && claim_available)
        {
            remote->owner_claimed = true;
            remote->control_attached = true;
            claimed = true;
        }
        else if (reuse_owner)
        {
            remote->control_attached = true;
            claimed = true;
        }
        accepted = remote->ops.load(
                       play.url, play.metadata[0] ? play.metadata : NULL,
                       remote->ops.user_data) &&
                   remote->ops.play(remote->ops.user_data);
        if (!accepted)
        {
            clear_owner_locked(remote);
            remote->generation++;
            remote_mutex_unlock(&remote->mutex);
            (void)remote->ops.stop(remote->ops.user_data);
            if (claimed && remote->ops.release_owner)
                remote->ops.release_owner(session_id, remote->ops.user_data);
            return airplay_rtsp_response_set_status(response, 503);
        }
        remote_mutex_unlock(&remote->mutex);
    }
    if (!accepted)
    {
        fail_generation(remote, session_id, generation);
        return airplay_rtsp_response_set_status(response, 503);
    }
    AIRPLAY_TRACE("[airplay-remote] session=%llu play mode=%s url-bytes=%zu start=%s\n",
                  (unsigned long long)session_id,
                  reverse_hls ? "reverse-hls" : "direct", strlen(play.url),
                  play.has_start ? (play.start_is_fraction ? "fraction" : "seconds")
                                 : "none");
    return true;
}

static bool handle_action(AirPlayRemoteVideo *remote, uint64_t session_id,
                          const AirPlayRtspRequest *request,
                          AirPlayRtspResponse *response)
{
    AirPlayRemoteHlsAction action = {0};
    AirPlayRemoteHlsActionResult action_result =
        AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_CONTENT_TYPE;
    char metadata[AIRPLAY_REMOTE_VIDEO_METADATA_MAX + 1u];
    bool pending_generation;
    bool accepted;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!content_type_is(request, "application/x-apple-binary-plist"))
    {
        AIRPLAY_TRACE(
            "[airplay-remote-hls] session=%llu action=rejected bytes=%zu "
            "reason=%s\n",
            (unsigned long long)session_id, request->body_length,
            airplay_remote_hls_action_result_name(action_result));
        return airplay_rtsp_response_set_status(response, 400);
    }
    if (!airplay_remote_hls_handle_action(
            remote->hls, session_id, request->body, request->body_length,
            &action, &action_result))
    {
        AIRPLAY_TRACE(
            "[airplay-remote-hls] session=%llu action=rejected bytes=%zu "
            "reason=%s\n",
            (unsigned long long)session_id, request->body_length,
            airplay_remote_hls_action_result_name(action_result));
        return airplay_rtsp_response_set_status(response, 400);
    }
    if (action.kind == AIRPLAY_REMOTE_HLS_ACTION_ACK)
        return true;
    if (action.kind == AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT)
    {
        remote_mutex_lock(&remote->mutex);
        pending_generation =
            remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
                             remote->owner_session_id == session_id &&
                             remote->generation == action.generation;
        remote_mutex_unlock(&remote->mutex);
        if (!pending_generation)
            return airplay_rtsp_response_set_status(response, 409);
        accepted = send_hls_event(remote, session_id, &action.event);
        airplay_remote_hls_event_clear(&action.event);
        if (!accepted)
        {
            fail_generation(remote, session_id, action.generation);
            return airplay_rtsp_response_set_status(response, 503);
        }
        return true;
    }

    if (!remote->ops.claim_owner)
    {
        remote_mutex_lock(&remote->mutex);
        pending_generation =
            remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
            remote->owner_session_id == session_id &&
            remote->generation == action.generation;
        remote_mutex_unlock(&remote->mutex);
        if (!pending_generation)
            return airplay_rtsp_response_set_status(response, 409);
        fail_generation(remote, session_id, action.generation);
        return airplay_rtsp_response_set_status(response, 503);
    }

    remote_mutex_lock(&remote->mutex);
    pending_generation =
        remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
                         remote->owner_session_id == session_id &&
                         remote->generation == action.generation;
    snprintf(metadata, sizeof(metadata), "%s", remote->pending_metadata);
    if (!pending_generation)
    {
        remote_mutex_unlock(&remote->mutex);
        return airplay_rtsp_response_set_status(response, 409);
    }
    if (!remote->ops.claim_owner(session_id, remote->ops.user_data))
    {
        remote_mutex_unlock(&remote->mutex);
        fail_generation(remote, session_id, action.generation);
        return airplay_rtsp_response_set_status(response, 409);
    }
    remote->state = AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE;
    remote->owner_claimed = true;
    remote->control_attached = true;
    accepted = remote->ops.load(action.playback_url,
                                metadata[0] ? metadata : NULL,
                                remote->ops.user_data) &&
               remote->ops.play(remote->ops.user_data);
    if (!accepted)
    {
        clear_owner_locked(remote);
        remote->generation++;
        remote_mutex_unlock(&remote->mutex);
        airplay_remote_hls_reset(remote->hls, session_id,
                                 action.generation);
        (void)remote->ops.stop(remote->ops.user_data);
        if (remote->ops.release_owner)
            remote->ops.release_owner(session_id, remote->ops.user_data);
        return airplay_rtsp_response_set_status(response, 503);
    }
    remote_mutex_unlock(&remote->mutex);
    AIRPLAY_TRACE(
        "[airplay-remote] session=%llu hls-ready generation=%u url=%s\n",
        (unsigned long long)session_id, action.generation,
        action.playback_url);
    return true;
}

static bool apply_pending_seek(AirPlayRemoteVideo *remote, uint64_t session_id,
                               const AirPlayRemoteVideoSnapshot *snapshot)
{
    int target_ms = 0;
    bool apply = false;

    if (!snapshot->seekable || snapshot->duration_ms <= 0)
        return true;
    remote_mutex_lock(&remote->mutex);
    if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
        remote->owner_session_id == session_id)
    {
        if (remote->pending_scrub)
        {
            target_ms = remote->pending_scrub_ms;
            apply = true;
        }
        else if (remote->pending_seek)
        {
            target_ms = remote->pending_seek_ms;
            apply = true;
        }
        else if (remote->pending_fraction_seek)
        {
            target_ms = (int)(remote->pending_fraction * snapshot->duration_ms +
                              0.5);
            apply = true;
        }
        remote->pending_scrub = false;
        remote->pending_seek = false;
        remote->pending_fraction_seek = false;
    }
    remote_mutex_unlock(&remote->mutex);
    if (!apply)
        return true;
    if (target_ms > snapshot->duration_ms)
        target_ms = snapshot->duration_ms;
    return remote->ops.seek_ms(target_ms, remote->ops.user_data);
}

static bool snapshot_for_session(AirPlayRemoteVideo *remote, uint64_t session_id,
                                 AirPlayRemoteVideoSnapshot *snapshot)
{
    if (!session_owned(remote, session_id) ||
        !remote->ops.snapshot(snapshot, remote->ops.user_data))
        return false;
    if (!apply_pending_seek(remote, session_id, snapshot))
        return false;
    return remote->ops.snapshot(snapshot, remote->ops.user_data);
}

static bool handle_rate(AirPlayRemoteVideo *remote, uint64_t session_id,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    double rate;
    bool ok;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!session_bound(remote, session_id))
        return airplay_rtsp_response_set_status(response, 409);
    if (!(query_number(request->uri, "value", 1.0, &rate) ||
          query_number(request->uri, "rate", 1.0, &rate)) ||
        (rate != 0.0 && rate != 1.0))
        return airplay_rtsp_response_set_status(response, 400);
    remote_mutex_lock(&remote->mutex);
    if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
        remote->owner_session_id == session_id)
    {
        remote->requested_rate = rate;
        remote_mutex_unlock(&remote->mutex);
        return true;
    }
    if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
        remote->owner_session_id == session_id)
        remote->requested_rate = rate;
    remote_mutex_unlock(&remote->mutex);
    ok = rate == 0.0 ? remote->ops.pause(remote->ops.user_data)
                     : remote->ops.play(remote->ops.user_data);
    if (!ok)
        return airplay_rtsp_response_set_status(response, 503);
    return true;
}

static bool handle_scrub(AirPlayRemoteVideo *remote, uint64_t session_id,
                         const AirPlayRtspRequest *request,
                         AirPlayRtspResponse *response)
{
    AirPlayRemoteVideoSnapshot snapshot = {0};
    double seconds;

    if (!session_bound(remote, session_id))
        return airplay_rtsp_response_set_status(response, 409);
    if (strcmp(request->method, "POST") == 0)
    {
        int position_ms;

        if (!query_number(request->uri, "position", (double)INT_MAX / 1000.0,
                          &seconds))
            return airplay_rtsp_response_set_status(response, 400);
        position_ms = (int)(seconds * 1000.0 + 0.5);
        remote_mutex_lock(&remote->mutex);
        if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
            remote->owner_session_id == session_id)
        {
            remote->pending_scrub_ms = position_ms;
            remote->pending_scrub = true;
            remote_mutex_unlock(&remote->mutex);
            return true;
        }
        if (remote->state != AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE ||
            remote->owner_session_id != session_id)
        {
            remote_mutex_unlock(&remote->mutex);
            return airplay_rtsp_response_set_status(response, 409);
        }
        remote_mutex_unlock(&remote->mutex);
        if (!remote->ops.seek_ms(position_ms, remote->ops.user_data))
            return airplay_rtsp_response_set_status(response, 503);
        remote_mutex_lock(&remote->mutex);
        if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
            remote->owner_session_id == session_id)
        {
            remote->pending_scrub = false;
            remote->pending_seek = false;
            remote->pending_fraction_seek = false;
        }
        remote_mutex_unlock(&remote->mutex);
        return true;
    }
    if (strcmp(request->method, "GET") == 0)
    {
        char body[128];
        int written;

        if (!snapshot_for_session(remote, session_id, &snapshot))
            return airplay_rtsp_response_set_status(response, 503);
        written = snprintf(body, sizeof(body),
                           "duration: %.6f\r\nposition: %.6f\r\n",
                           snapshot.duration_ms / 1000.0,
                           snapshot.position_ms / 1000.0);
        return written > 0 && (size_t)written < sizeof(body) &&
               airplay_rtsp_response_set_body(response, body, (size_t)written,
                                               "text/parameters");
    }
    return airplay_rtsp_response_set_status(response, 405);
}

static bool handle_playback_info(AirPlayRemoteVideo *remote,
                                 uint64_t session_id,
                                 const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    AirPlayRemoteVideoSnapshot snapshot = {0};
    char body[AIRPLAY_REMOTE_XML_MAX];
    char seekable[256];
    const char *ready;
    const char *empty;
    const char *full;
    double rate;
    double duration;
    double position;
    double loaded_start;
    double loaded_duration;
    double requested_rate;
    int written;

    if (strcmp(request->method, "GET") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (session_hls_pending(remote, session_id))
        snapshot.state = AIRPLAY_REMOTE_VIDEO_LOADING;
    else if (!snapshot_for_session(remote, session_id, &snapshot))
        return airplay_rtsp_response_set_status(response, 503);
    duration = snapshot.duration_ms > 0 ? snapshot.duration_ms / 1000.0 : 0.0;
    position = snapshot.position_ms > 0 ? snapshot.position_ms / 1000.0 : 0.0;
    loaded_start = position < duration ? position : duration;
    loaded_duration = duration - loaded_start;
    remote_mutex_lock(&remote->mutex);
    requested_rate = remote->requested_rate;
    remote_mutex_unlock(&remote->mutex);
    rate = snapshot.state == AIRPLAY_REMOTE_VIDEO_PLAYING
               ? 1.0
               : (snapshot.state == AIRPLAY_REMOTE_VIDEO_LOADING ||
                          snapshot.state == AIRPLAY_REMOTE_VIDEO_BUFFERING
                      ? requested_rate
                      : 0.0);
    ready = snapshot.has_media && snapshot.state != AIRPLAY_REMOTE_VIDEO_ERROR
                ? "<true/>"
                : "<false/>";
    empty = snapshot.state == AIRPLAY_REMOTE_VIDEO_LOADING ||
                    snapshot.state == AIRPLAY_REMOTE_VIDEO_BUFFERING
                ? "<true/>"
                : "<false/>";
    full = snapshot.state == AIRPLAY_REMOTE_VIDEO_LOADING ||
                   snapshot.state == AIRPLAY_REMOTE_VIDEO_BUFFERING
               ? "<false/>"
               : "<true/>";
    if (snapshot.seekable)
    {
        int range_written = snprintf(
            seekable, sizeof(seekable),
            "<dict><key>start</key><real>0.0</real><key>duration</key>"
            "<real>%.6f</real></dict>",
            duration);
        if (range_written <= 0 || (size_t)range_written >= sizeof(seekable))
            return airplay_rtsp_response_set_status(response, 500);
    }
    else
        seekable[0] = '\0';
    written = snprintf(
        body, sizeof(body),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>duration</key><real>%.6f</real>"
        "<key>position</key><real>%.6f</real>"
        "<key>rate</key><real>%.1f</real>"
        "<key>readyToPlay</key>%s"
        "<key>playbackBufferEmpty</key>%s"
        "<key>playbackBufferFull</key>%s"
        "<key>playbackLikelyToKeepUp</key>%s"
        "<key>loadedTimeRanges</key><array><dict>"
        "<key>start</key><real>%.6f</real><key>duration</key><real>%.6f</real>"
        "</dict></array>"
        "<key>seekableTimeRanges</key><array>%s</array>"
        "</dict></plist>\n",
        duration, position, rate, ready, empty, full, full, loaded_start,
        loaded_duration,
        seekable);
    if (written <= 0 || (size_t)written >= sizeof(body))
        return airplay_rtsp_response_set_status(response, 500);
    AIRPLAY_TRACE(
        "[airplay-remote] session=%llu playback-info state=%d "
        "duration=%.3f position=%.3f rate=%.1f ready=%d empty=%d "
        "full=%d loaded=%.3f+%.3f seekable=%d\n",
        (unsigned long long)session_id, (int)snapshot.state, duration,
        position, rate, ready[1] == 't' ? 1 : 0,
        empty[1] == 't' ? 1 : 0, full[1] == 't' ? 1 : 0,
        loaded_start, loaded_duration, snapshot.seekable ? 1 : 0);
    return airplay_rtsp_response_set_body(response, body, (size_t)written,
                                           "text/x-apple-plist+xml");
}

static bool terminate_session(AirPlayRemoteVideo *remote, uint64_t session_id,
                              bool *stopped_out)
{
    bool active = false;
    bool pending = false;
    bool release_owner = false;
    uint32_t generation = 0u;
    bool stopped = true;

    if (!remote || !session_id || !stopped_out)
        return false;
    remote_mutex_lock(&remote->mutex);
    active = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
             remote->owner_session_id == session_id;
    pending = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS &&
              remote->owner_session_id == session_id;
    if (active || pending)
    {
        generation = remote->generation;
        release_owner = remote->owner_claimed;
        clear_owner_locked(remote);
        remote->generation++;
    }
    remote_mutex_unlock(&remote->mutex);
    if (!active && !pending)
        return false;
    airplay_remote_hls_reset(remote->hls, session_id, generation);
    if (active)
        stopped = remote->ops.stop(remote->ops.user_data);
    if (release_owner && remote->ops.release_owner)
        remote->ops.release_owner(session_id, remote->ops.user_data);
    *stopped_out = stopped;
    return true;
}

static bool handle_stop(AirPlayRemoteVideo *remote, uint64_t session_id,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    bool stopped;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!terminate_session(remote, session_id, &stopped))
        return airplay_rtsp_response_set_status(response, 409);
    return stopped ? true : airplay_rtsp_response_set_status(response, 503);
}

bool airplay_remote_video_create(const AirPlayRemoteVideoOps *ops,
                                 AirPlayRemoteVideo **remote_out)
{
    AirPlayRemoteVideo *remote;

    if (!ops || !remote_out || *remote_out || !ops->load || !ops->play ||
        !ops->pause || !ops->stop || !ops->seek_ms || !ops->snapshot)
        return false;
    remote = calloc(1, sizeof(*remote));
    if (!remote)
        return false;
    if (!remote_mutex_init(&remote->mutex))
    {
        free(remote);
        return false;
    }
    remote->mutex_ready = true;
    if (!airplay_remote_hls_create(&remote->hls))
    {
        remote_mutex_destroy(&remote->mutex);
        free(remote);
        return false;
    }
    remote->ops = *ops;
    *remote_out = remote;
    return true;
}

void airplay_remote_video_destroy(AirPlayRemoteVideo *remote)
{
    bool stop;
    bool release_owner;
    uint64_t session_id;

    if (!remote)
        return;
    remote_mutex_lock(&remote->mutex);
    stop = remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE;
    release_owner = remote->owner_claimed;
    session_id = remote->owner_session_id;
    clear_owner_locked(remote);
    remote_mutex_unlock(&remote->mutex);
    if (stop)
    {
        (void)remote->ops.stop(remote->ops.user_data);
        if (release_owner && remote->ops.release_owner)
            remote->ops.release_owner(session_id, remote->ops.user_data);
    }
    airplay_remote_hls_destroy(remote->hls);
    remote->hls = NULL;
    if (remote->mutex_ready)
        remote_mutex_destroy(&remote->mutex);
    memset(remote, 0, sizeof(*remote));
    free(remote);
}

bool airplay_remote_video_route(AirPlayRemoteVideo *remote, uint64_t session_id,
                                const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response,
                                bool *handled_out)
{
    const char *uri;
    bool control_reattached = false;

    if (!remote || !session_id || !request || !response || !handled_out)
        return false;
    remote_mutex_lock(&remote->mutex);
    if (remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
        remote->owner_session_id == session_id &&
        !remote->control_attached)
    {
        remote->control_attached = true;
        control_reattached = true;
    }
    remote_mutex_unlock(&remote->mutex);
    if (control_reattached && remote->ops.control_attached)
        remote->ops.control_attached(session_id, remote->ops.user_data);
    *handled_out = true;
    uri = request->uri;
    if (strcmp(uri, "/play") == 0)
        return handle_play(remote, session_id, request, response);
    if (strcmp(uri, "/action") == 0)
        return handle_action(remote, session_id, request, response);
    if (strncmp(uri, "/rate?", 6u) == 0 || strcmp(uri, "/rate") == 0)
        return handle_rate(remote, session_id, request, response);
    if (strncmp(uri, "/scrub?", 7u) == 0 || strcmp(uri, "/scrub") == 0)
        return handle_scrub(remote, session_id, request, response);
    if (strcmp(uri, "/playback-info") == 0)
        return handle_playback_info(remote, session_id, request, response);
    if (strcmp(uri, "/stop") == 0)
        return handle_stop(remote, session_id, request, response);
    *handled_out = false;
    return true;
}

bool airplay_remote_video_is_local_uri(const char *uri)
{
    return airplay_remote_hls_is_local_uri(uri);
}

bool airplay_remote_video_route_local(AirPlayRemoteVideo *remote,
                                      const AirPlayRtspRequest *request,
                                      AirPlayRtspResponse *response,
                                      bool *handled_out)
{
    if (!remote)
        return false;
    return airplay_remote_hls_serve(remote->hls, request, response,
                                    handled_out);
}

void airplay_remote_video_session_closed(AirPlayRemoteVideo *remote,
                                         uint64_t session_id)
{
    bool pending = false;
    bool control_detached = false;
    uint32_t generation = 0u;

    if (!remote || !session_id)
        return;
    remote_mutex_lock(&remote->mutex);
    if (remote->owner_session_id == session_id &&
        remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_PENDING_HLS)
    {
        pending = true;
        generation = remote->generation;
        clear_owner_locked(remote);
        remote->generation++;
    }
    else if (remote->owner_session_id == session_id &&
             remote->state == AIRPLAY_REMOTE_VIDEO_SESSION_ACTIVE &&
             remote->control_attached)
    {
        remote->control_attached = false;
        control_detached = true;
    }
    remote_mutex_unlock(&remote->mutex);
    if (pending)
    {
        AIRPLAY_TRACE(
            "[airplay-remote] session=%llu logical-close generation=%u "
            "state=cancelled-pending\n",
            (unsigned long long)session_id, generation);
    }
    if (pending)
        airplay_remote_hls_reset(remote->hls, session_id, generation);
    if (control_detached && remote->ops.control_detached)
        remote->ops.control_detached(session_id, remote->ops.user_data);
}

bool airplay_remote_video_terminate_session(AirPlayRemoteVideo *remote,
                                            uint64_t session_id)
{
    bool stopped = true;

    return terminate_session(remote, session_id, &stopped) && stopped;
}

bool airplay_remote_video_relinquish_session(AirPlayRemoteVideo *remote,
                                             uint64_t session_id)
{
    uint32_t generation = 0u;
    bool relinquished = false;

    if (!remote || !session_id)
        return false;
    remote_mutex_lock(&remote->mutex);
    if (remote->owner_session_id == session_id &&
        remote->state != AIRPLAY_REMOTE_VIDEO_SESSION_IDLE)
    {
        generation = remote->generation;
        clear_owner_locked(remote);
        remote->generation++;
        relinquished = true;
    }
    remote_mutex_unlock(&remote->mutex);
    if (relinquished)
        airplay_remote_hls_reset(remote->hls, session_id, generation);
    return relinquished;
}
