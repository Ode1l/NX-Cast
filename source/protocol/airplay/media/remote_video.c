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

struct AirPlayRemoteVideo
{
    AirPlayRemoteVideoOps ops;
    AirPlayRemoteMutex mutex;
    uint64_t owner_session_id;
    uint32_t generation;
    int pending_seek_ms;
    double pending_fraction;
    double requested_rate;
    bool pending_seek;
    bool pending_fraction_seek;
    bool active;
    bool mutex_ready;
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
    AirPlayPlistValue *root = NULL;
    const AirPlayPlistValue *value;
    AirPlayPlistError error;
    const char *text;
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
    value = airplay_plist_dict_get(root, "clientProcName");
    text = airplay_plist_get_string(value);
    if (text && !copy_bounded(play->metadata, sizeof(play->metadata), text,
                              strlen(text)))
        goto cleanup;
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
    return airplay_remote_video_url_supported(play->url);
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
    owned = remote->active && remote->owner_session_id == session_id;
    remote_mutex_unlock(&remote->mutex);
    return owned;
}

static void clear_owner_locked(AirPlayRemoteVideo *remote)
{
    remote->owner_session_id = 0u;
    remote->pending_seek_ms = 0;
    remote->pending_fraction = 0.0;
    remote->requested_rate = 0.0;
    remote->pending_seek = false;
    remote->pending_fraction_seek = false;
    remote->active = false;
}

static bool handle_play(AirPlayRemoteVideo *remote, uint64_t session_id,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    AirPlayRemotePlayRequest play;
    uint32_t generation;
    bool accepted = false;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!parse_play_request(request, &play))
        return airplay_rtsp_response_set_status(response, 400);
    remote_mutex_lock(&remote->mutex);
    if (remote->active && remote->owner_session_id != session_id)
    {
        remote_mutex_unlock(&remote->mutex);
        return airplay_rtsp_response_set_status(response, 409);
    }
    remote->active = true;
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
    remote_mutex_unlock(&remote->mutex);

    if (remote->ops.claim_owner &&
        !remote->ops.claim_owner(session_id, remote->ops.user_data))
    {
        remote_mutex_lock(&remote->mutex);
        if (remote->generation == generation &&
            remote->owner_session_id == session_id)
            clear_owner_locked(remote);
        remote_mutex_unlock(&remote->mutex);
        return airplay_rtsp_response_set_status(response, 409);
    }

    accepted = remote->ops.load(play.url,
                                play.metadata[0] ? play.metadata : NULL,
                                remote->ops.user_data) &&
               remote->ops.play(remote->ops.user_data);
    if (!accepted)
    {
        remote_mutex_lock(&remote->mutex);
        if (remote->generation == generation &&
            remote->owner_session_id == session_id)
            clear_owner_locked(remote);
        remote_mutex_unlock(&remote->mutex);
        (void)remote->ops.stop(remote->ops.user_data);
        if (remote->ops.release_owner)
            remote->ops.release_owner(session_id, remote->ops.user_data);
        return airplay_rtsp_response_set_status(response, 503);
    }
    AIRPLAY_TRACE("[airplay-remote] session=%llu play url-bytes=%zu start=%s\n",
                  (unsigned long long)session_id, strlen(play.url),
                  play.has_start ? (play.start_is_fraction ? "fraction" : "seconds")
                                 : "none");
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
    if (remote->active && remote->owner_session_id == session_id)
    {
        if (remote->pending_seek)
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
    if (!session_owned(remote, session_id))
        return airplay_rtsp_response_set_status(response, 409);
    if (!(query_number(request->uri, "value", 1.0, &rate) ||
          query_number(request->uri, "rate", 1.0, &rate)) ||
        (rate != 0.0 && rate != 1.0))
        return airplay_rtsp_response_set_status(response, 400);
    ok = rate == 0.0 ? remote->ops.pause(remote->ops.user_data)
                     : remote->ops.play(remote->ops.user_data);
    if (!ok)
        return airplay_rtsp_response_set_status(response, 503);
    remote_mutex_lock(&remote->mutex);
    if (remote->active && remote->owner_session_id == session_id)
        remote->requested_rate = rate;
    remote_mutex_unlock(&remote->mutex);
    return true;
}

static bool handle_scrub(AirPlayRemoteVideo *remote, uint64_t session_id,
                         const AirPlayRtspRequest *request,
                         AirPlayRtspResponse *response)
{
    AirPlayRemoteVideoSnapshot snapshot = {0};
    double seconds;

    if (!session_owned(remote, session_id))
        return airplay_rtsp_response_set_status(response, 409);
    if (strcmp(request->method, "POST") == 0)
    {
        int position_ms;

        if (!query_number(request->uri, "position", (double)INT_MAX / 1000.0,
                          &seconds))
            return airplay_rtsp_response_set_status(response, 400);
        position_ms = (int)(seconds * 1000.0 + 0.5);
        if (!remote->ops.seek_ms(position_ms, remote->ops.user_data))
            return airplay_rtsp_response_set_status(response, 503);
        remote_mutex_lock(&remote->mutex);
        remote->pending_seek = false;
        remote->pending_fraction_seek = false;
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
    double requested_rate;
    int written;

    if (strcmp(request->method, "GET") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    if (!snapshot_for_session(remote, session_id, &snapshot))
        return airplay_rtsp_response_set_status(response, 503);
    duration = snapshot.duration_ms > 0 ? snapshot.duration_ms / 1000.0 : 0.0;
    position = snapshot.position_ms > 0 ? snapshot.position_ms / 1000.0 : 0.0;
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
        "<key>start</key><real>0.0</real><key>duration</key><real>%.6f</real>"
        "</dict></array>"
        "<key>seekableTimeRanges</key><array>%s</array>"
        "</dict></plist>\n",
        duration, position, rate, ready, empty, full, full, duration,
        seekable);
    if (written <= 0 || (size_t)written >= sizeof(body))
        return airplay_rtsp_response_set_status(response, 500);
    return airplay_rtsp_response_set_body(response, body, (size_t)written,
                                           "text/x-apple-plist+xml");
}

static bool handle_stop(AirPlayRemoteVideo *remote, uint64_t session_id,
                        const AirPlayRtspRequest *request,
                        AirPlayRtspResponse *response)
{
    bool stop;

    if (strcmp(request->method, "POST") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    remote_mutex_lock(&remote->mutex);
    stop = remote->active && remote->owner_session_id == session_id;
    if (stop)
    {
        clear_owner_locked(remote);
        remote->generation++;
    }
    remote_mutex_unlock(&remote->mutex);
    if (!stop)
        return airplay_rtsp_response_set_status(response, 409);
    stop = remote->ops.stop(remote->ops.user_data);
    if (remote->ops.release_owner)
        remote->ops.release_owner(session_id, remote->ops.user_data);
    return stop ? true : airplay_rtsp_response_set_status(response, 503);
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
    remote->ops = *ops;
    *remote_out = remote;
    return true;
}

void airplay_remote_video_destroy(AirPlayRemoteVideo *remote)
{
    bool stop;
    uint64_t session_id;

    if (!remote)
        return;
    remote_mutex_lock(&remote->mutex);
    stop = remote->active;
    session_id = remote->owner_session_id;
    clear_owner_locked(remote);
    remote_mutex_unlock(&remote->mutex);
    if (stop)
    {
        (void)remote->ops.stop(remote->ops.user_data);
        if (remote->ops.release_owner)
            remote->ops.release_owner(session_id, remote->ops.user_data);
    }
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

    if (!remote || !session_id || !request || !response || !handled_out)
        return false;
    *handled_out = true;
    uri = request->uri;
    if (strcmp(uri, "/play") == 0)
        return handle_play(remote, session_id, request, response);
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

void airplay_remote_video_session_closed(AirPlayRemoteVideo *remote,
                                         uint64_t session_id)
{
    bool stop;

    if (!remote || !session_id)
        return;
    remote_mutex_lock(&remote->mutex);
    stop = remote->active && remote->owner_session_id == session_id;
    if (stop)
    {
        clear_owner_locked(remote);
        remote->generation++;
    }
    remote_mutex_unlock(&remote->mutex);
    if (stop)
    {
        (void)remote->ops.stop(remote->ops.user_data);
        if (remote->ops.release_owner)
            remote->ops.release_owner(session_id, remote->ops.user_data);
    }
}
