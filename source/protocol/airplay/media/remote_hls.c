#include "remote_hls.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex AirPlayRemoteHlsMutex;
#else
#include <pthread.h>
typedef pthread_mutex_t AirPlayRemoteHlsMutex;
#endif

#include "protocol/airplay/protocol/plist.h"

#define AIRPLAY_REMOTE_HLS_MAX_PLAYLIST_BYTES (512u * 1024u)
#define AIRPLAY_REMOTE_HLS_MAX_REWRITTEN_BYTES AIRPLAY_RTSP_MAX_BODY_BYTES
#define AIRPLAY_REMOTE_HLS_MAX_MEDIA_PLAYLISTS 16u
#define AIRPLAY_REMOTE_HLS_TOKEN_MAX 31u
#define AIRPLAY_REMOTE_HLS_LOCAL_PREFIX "/airplay-hls/"
#define AIRPLAY_REMOTE_HLS_CONTENT_TYPE "application/vnd.apple.mpegurl"

typedef struct
{
    char source_url[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
    uint8_t *playlist;
    size_t playlist_length;
} AirPlayRemoteHlsMedia;

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    size_t limit;
} AirPlayRemoteHlsBuffer;

struct AirPlayRemoteHls
{
    AirPlayRemoteHlsMutex mutex;
    uint64_t session_id;
    uint32_t generation;
    uint32_t next_request_id;
    uint32_t expected_request_id;
    uint16_t control_port;
    size_t pending_media_index;
    size_t media_count;
    char token[AIRPLAY_REMOTE_HLS_TOKEN_MAX + 1u];
    char apple_session_id[AIRPLAY_REMOTE_HLS_APPLE_SESSION_MAX + 1u];
    char master_source[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
    char expected_url[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
    char playback_url[AIRPLAY_REMOTE_HLS_LOCAL_URL_MAX + 1u];
    uint8_t *master_playlist;
    size_t master_playlist_length;
    AirPlayRemoteHlsMedia media[AIRPLAY_REMOTE_HLS_MAX_MEDIA_PLAYLISTS];
    bool active;
    bool expecting_master;
    bool mutex_ready;
};

static bool hls_mutex_init(AirPlayRemoteHlsMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void hls_mutex_destroy(AirPlayRemoteHlsMutex *mutex)
{
#ifndef __SWITCH__
    (void)pthread_mutex_destroy(mutex);
#else
    (void)mutex;
#endif
}

static void hls_mutex_lock(AirPlayRemoteHlsMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    (void)pthread_mutex_lock(mutex);
#endif
}

static void hls_mutex_unlock(AirPlayRemoteHlsMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    (void)pthread_mutex_unlock(mutex);
#endif
}

static bool hls_copy(char *output, size_t output_size, const char *input)
{
    size_t length;

    if (!output || output_size == 0u || !input)
        return false;
    length = strlen(input);
    if (length >= output_size)
        return false;
    memcpy(output, input, length + 1u);
    return true;
}

static bool hls_text_safe(const char *text, size_t maximum)
{
    size_t length = 0u;

    if (!text || *text == '\0')
        return false;
    while (text[length] != '\0')
    {
        unsigned char character = (unsigned char)text[length];

        if (character < 0x21u || character > 0x7eu || length >= maximum)
            return false;
        length++;
    }
    return true;
}

static bool hls_buffer_reserve(AirPlayRemoteHlsBuffer *buffer,
                               size_t additional)
{
    size_t required;
    size_t capacity;
    char *next;

    if (!buffer || additional > buffer->limit - buffer->length)
        return false;
    required = buffer->length + additional;
    if (required <= buffer->capacity)
        return true;
    capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required)
    {
        if (capacity > buffer->limit / 2u)
        {
            capacity = buffer->limit;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required || capacity > buffer->limit)
        return false;
    next = realloc(buffer->data, capacity + 1u);
    if (!next)
        return false;
    buffer->data = next;
    buffer->capacity = capacity;
    return true;
}

static bool hls_buffer_append(AirPlayRemoteHlsBuffer *buffer,
                              const void *bytes, size_t length)
{
    if (!buffer || (length != 0u && !bytes) ||
        !hls_buffer_reserve(buffer, length))
        return false;
    if (length != 0u)
        memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool hls_buffer_append_text(AirPlayRemoteHlsBuffer *buffer,
                                   const char *text)
{
    return text && hls_buffer_append(buffer, text, strlen(text));
}

static void hls_buffer_clear(AirPlayRemoteHlsBuffer *buffer)
{
    if (!buffer)
        return;
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static bool hls_xml_append_escaped(AirPlayRemoteHlsBuffer *buffer,
                                   const char *text)
{
    const char *cursor = text;
    const char *start = text;

    if (!buffer || !text)
        return false;
    while (*cursor)
    {
        const char *replacement = NULL;

        switch (*cursor)
        {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '\"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        default:
            break;
        }
        if (replacement)
        {
            if (!hls_buffer_append(buffer, start, (size_t)(cursor - start)) ||
                !hls_buffer_append_text(buffer, replacement))
                return false;
            start = cursor + 1;
        }
        cursor++;
    }
    return hls_buffer_append(buffer, start, (size_t)(cursor - start));
}

static bool hls_build_event_body(const char *url, uint32_t request_id,
                                 const char *apple_session_id,
                                 uint8_t **body_out, size_t *length_out)
{
    static const char prefix[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>sessionID</key><integer>1</integer>"
        "<key>type</key><string>unhandledURLRequest</string>"
        "<key>request</key><dict>"
        "<key>FCUP_Response_ClientInfo</key><integer>1</integer>"
        "<key>FCUP_Response_ClientRef</key><integer>40030004</integer>"
        "<key>FCUP_Response_RequestID</key><integer>";
    static const char request_url[] =
        "</integer><key>FCUP_Response_URL</key><string>";
    static const char headers[] =
        "</string><key>sessionID</key><integer>1</integer>"
        "<key>FCUP_Response_Headers</key><dict>"
        "<key>X-Playback-Session-Id</key><string>";
    static const char suffix[] =
        "</string><key>User-Agent</key>"
        "<string>AppleCoreMedia/1.0.0.11B554a (Apple TV; U; CPU OS 7_0_4 "
        "like Mac OS X; en_us)</string></dict></dict></dict></plist>\n";
    AirPlayRemoteHlsBuffer output = {.limit = 64u * 1024u};
    char request_id_text[16];
    int written;
    bool ok;

    if (!url || !apple_session_id || !body_out || !length_out)
        return false;
    *body_out = NULL;
    *length_out = 0u;
    written = snprintf(request_id_text, sizeof(request_id_text), "%u",
                       request_id);
    if (written <= 0 || (size_t)written >= sizeof(request_id_text))
        return false;
    ok = hls_buffer_append_text(&output, prefix) &&
         hls_buffer_append_text(&output, request_id_text) &&
         hls_buffer_append_text(&output, request_url) &&
         hls_xml_append_escaped(&output, url) &&
         hls_buffer_append_text(&output, headers) &&
         hls_xml_append_escaped(&output, apple_session_id) &&
         hls_buffer_append_text(&output, suffix);
    if (!ok)
    {
        hls_buffer_clear(&output);
        return false;
    }
    *body_out = (uint8_t *)output.data;
    *length_out = output.length;
    return true;
}

static void hls_reset_locked(AirPlayRemoteHls *hls)
{
    size_t index;

    free(hls->master_playlist);
    hls->master_playlist = NULL;
    hls->master_playlist_length = 0u;
    for (index = 0u; index < AIRPLAY_REMOTE_HLS_MAX_MEDIA_PLAYLISTS; ++index)
    {
        free(hls->media[index].playlist);
        memset(&hls->media[index], 0, sizeof(hls->media[index]));
    }
    hls->session_id = 0u;
    hls->generation = 0u;
    hls->next_request_id = 0u;
    hls->expected_request_id = 0u;
    hls->control_port = 0u;
    hls->pending_media_index = 0u;
    hls->media_count = 0u;
    hls->token[0] = '\0';
    hls->apple_session_id[0] = '\0';
    hls->master_source[0] = '\0';
    hls->expected_url[0] = '\0';
    hls->playback_url[0] = '\0';
    hls->active = false;
    hls->expecting_master = false;
}

bool airplay_remote_hls_create(AirPlayRemoteHls **hls_out)
{
    AirPlayRemoteHls *hls;

    if (!hls_out)
        return false;
    *hls_out = NULL;
    hls = calloc(1u, sizeof(*hls));
    if (!hls || !hls_mutex_init(&hls->mutex))
    {
        free(hls);
        return false;
    }
    hls->mutex_ready = true;
    *hls_out = hls;
    return true;
}

void airplay_remote_hls_destroy(AirPlayRemoteHls *hls)
{
    if (!hls)
        return;
    hls_mutex_lock(&hls->mutex);
    hls_reset_locked(hls);
    hls_mutex_unlock(&hls->mutex);
    if (hls->mutex_ready)
        hls_mutex_destroy(&hls->mutex);
    memset(hls, 0, sizeof(*hls));
    free(hls);
}

bool airplay_remote_hls_locator_supported(const char *locator)
{
    const char *master;

    if (!hls_text_safe(locator, AIRPLAY_REMOTE_HLS_URL_MAX))
        return false;
    if (strncasecmp(locator, "http://", 7u) == 0 ||
        strncasecmp(locator, "https://", 8u) == 0)
        return false;
    master = strstr(locator, "/master.m3u8");
    return master &&
           (master[12] == '\0' || master[12] == '?' || master[12] == '#');
}

static bool hls_prepare_event_locked(AirPlayRemoteHls *hls,
                                     const char *url,
                                     AirPlayRemoteHlsEvent *event_out)
{
    uint32_t request_id;

    if (!hls || !url || !event_out || !hls->active)
        return false;
    memset(event_out, 0, sizeof(*event_out));
    request_id = ++hls->next_request_id;
    if (request_id == 0u)
        request_id = ++hls->next_request_id;
    if (!hls_copy(hls->expected_url, sizeof(hls->expected_url), url) ||
        !hls_copy(event_out->apple_session_id,
                  sizeof(event_out->apple_session_id),
                  hls->apple_session_id) ||
        !hls_build_event_body(url, request_id, hls->apple_session_id,
                              &event_out->body, &event_out->body_length))
    {
        return false;
    }
    hls->expected_request_id = request_id;
    event_out->request_id = request_id;
    return true;
}

bool airplay_remote_hls_begin(AirPlayRemoteHls *hls,
                              uint64_t session_id,
                              uint32_t generation,
                              uint16_t control_port,
                              const char *apple_session_id,
                              const char *locator,
                              AirPlayRemoteHlsEvent *event_out)
{
    int written;
    bool ok;

    if (!hls || session_id == 0u || generation == 0u || control_port == 0u ||
        !hls_text_safe(apple_session_id, AIRPLAY_REMOTE_HLS_APPLE_SESSION_MAX) ||
        !airplay_remote_hls_locator_supported(locator) || !event_out)
        return false;
    memset(event_out, 0, sizeof(*event_out));
    hls_mutex_lock(&hls->mutex);
    hls_reset_locked(hls);
    hls->session_id = session_id;
    hls->generation = generation;
    hls->control_port = control_port;
    hls->active = true;
    hls->expecting_master = true;
    written = snprintf(hls->token, sizeof(hls->token), "%016llx-%08x",
                       (unsigned long long)session_id, generation);
    ok = written > 0 && (size_t)written < sizeof(hls->token) &&
         hls_copy(hls->apple_session_id, sizeof(hls->apple_session_id),
                  apple_session_id) &&
         hls_copy(hls->master_source, sizeof(hls->master_source), locator);
    if (ok)
    {
        written = snprintf(hls->playback_url, sizeof(hls->playback_url),
                           "http://127.0.0.1:%u%s%s/master.m3u8",
                           control_port, AIRPLAY_REMOTE_HLS_LOCAL_PREFIX,
                           hls->token);
        ok = written > 0 && (size_t)written < sizeof(hls->playback_url) &&
             hls_prepare_event_locked(hls, locator, event_out);
    }
    if (!ok)
    {
        airplay_remote_hls_event_clear(event_out);
        hls_reset_locked(hls);
    }
    hls_mutex_unlock(&hls->mutex);
    return ok;
}

static bool hls_has_scheme(const char *uri)
{
    const unsigned char *cursor = (const unsigned char *)uri;

    if (!cursor || !isalpha(*cursor))
        return false;
    cursor++;
    while (*cursor && *cursor != ':')
    {
        if (!isalnum(*cursor) && *cursor != '+' && *cursor != '-' &&
            *cursor != '.')
            return false;
        cursor++;
    }
    return *cursor == ':';
}

static bool hls_is_http_url(const char *uri)
{
    return uri && (strncasecmp(uri, "http://", 7u) == 0 ||
                   strncasecmp(uri, "https://", 8u) == 0);
}

static bool hls_resolve_uri(const char *base, const char *reference,
                            char output[AIRPLAY_REMOTE_HLS_URL_MAX + 1u])
{
    const char *scheme_end;
    const char *authority_end;
    const char *directory_end;
    int written;

    if (!hls_text_safe(base, AIRPLAY_REMOTE_HLS_URL_MAX) ||
        !hls_text_safe(reference, AIRPLAY_REMOTE_HLS_URL_MAX))
        return false;
    if (hls_has_scheme(reference))
        return hls_copy(output, AIRPLAY_REMOTE_HLS_URL_MAX + 1u, reference);
    scheme_end = strstr(base, "://");
    if (reference[0] == '/' && reference[1] == '/')
    {
        if (!scheme_end)
            return false;
        written = snprintf(output, AIRPLAY_REMOTE_HLS_URL_MAX + 1u,
                           "%.*s:%s", (int)(scheme_end - base), base,
                           reference);
        return written > 0 && written <= (int)AIRPLAY_REMOTE_HLS_URL_MAX;
    }
    if (reference[0] == '/' && scheme_end)
    {
        authority_end = strchr(scheme_end + 3, '/');
        if (!authority_end)
            authority_end = base + strcspn(base, "?#");
        written = snprintf(output, AIRPLAY_REMOTE_HLS_URL_MAX + 1u,
                           "%.*s%s", (int)(authority_end - base), base,
                           reference);
        return written > 0 && written <= (int)AIRPLAY_REMOTE_HLS_URL_MAX;
    }
    directory_end = strrchr(base, '/');
    if (!directory_end)
        return false;
    written = snprintf(output, AIRPLAY_REMOTE_HLS_URL_MAX + 1u, "%.*s/%s",
                       (int)(directory_end - base), base, reference);
    return written > 0 && written <= (int)AIRPLAY_REMOTE_HLS_URL_MAX;
}

static bool hls_uri_is_playlist(const char *uri)
{
    const char *suffix = uri ? strstr(uri, ".m3u8") : NULL;

    return suffix && (suffix[5] == '\0' || suffix[5] == '?' ||
                      suffix[5] == '#');
}

static bool hls_playlist_valid(const uint8_t *bytes, size_t length)
{
    size_t offset = 0u;
    size_t index;

    if (!bytes || length < 7u ||
        length > AIRPLAY_REMOTE_HLS_MAX_PLAYLIST_BYTES)
        return false;
    if (length >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu &&
        bytes[2] == 0xbfu)
        offset = 3u;
    if (length - offset < 7u || memcmp(bytes + offset, "#EXTM3U", 7u) != 0)
        return false;
    for (index = offset; index < length; ++index)
    {
        if (bytes[index] == '\0' ||
            (bytes[index] < 0x20u && bytes[index] != '\r' &&
             bytes[index] != '\n' && bytes[index] != '\t'))
            return false;
    }
    return true;
}

static bool hls_register_media_locked(AirPlayRemoteHls *hls,
                                      const char *base,
                                      const char *reference,
                                      size_t *index_out)
{
    char resolved[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
    size_t index;

    if (!hls_resolve_uri(base, reference, resolved))
        return false;
    for (index = 0u; index < hls->media_count; ++index)
    {
        if (strcmp(hls->media[index].source_url, resolved) == 0)
        {
            *index_out = index;
            return true;
        }
    }
    if (hls->media_count >= AIRPLAY_REMOTE_HLS_MAX_MEDIA_PLAYLISTS)
        return false;
    index = hls->media_count++;
    if (!hls_copy(hls->media[index].source_url,
                  sizeof(hls->media[index].source_url), resolved))
        return false;
    *index_out = index;
    return true;
}

static bool hls_find_uri_attribute(const char *line, size_t length,
                                   size_t *value_start_out,
                                   size_t *value_end_out)
{
    size_t index;

    for (index = 0u; index + 5u <= length; ++index)
    {
        size_t end;

        if (memcmp(line + index, "URI=\"", 5u) != 0)
            continue;
        end = index + 5u;
        while (end < length && line[end] != '\"')
            end++;
        if (end == length || end == index + 5u)
            return false;
        *value_start_out = index + 5u;
        *value_end_out = end;
        return true;
    }
    return false;
}

static bool hls_append_local_media_uri(AirPlayRemoteHlsBuffer *output,
                                       size_t index)
{
    char local[32];
    int written = snprintf(local, sizeof(local), "media/%zu.m3u8", index);

    return written > 0 && (size_t)written < sizeof(local) &&
           hls_buffer_append(output, local, (size_t)written);
}

static bool hls_rewrite_master_locked(AirPlayRemoteHls *hls,
                                      const uint8_t *bytes, size_t length,
                                      uint8_t **playlist_out,
                                      size_t *playlist_length_out)
{
    AirPlayRemoteHlsBuffer output = {
        .limit = AIRPLAY_REMOTE_HLS_MAX_REWRITTEN_BYTES};
    size_t offset = 0u;
    bool ok = true;

    while (offset < length && ok)
    {
        size_t line_start = offset;
        size_t line_end;
        size_t content_end;
        const char *line;
        size_t line_length;

        while (offset < length && bytes[offset] != '\n')
            offset++;
        line_end = offset;
        if (offset < length)
            offset++;
        content_end = line_end;
        if (content_end > line_start && bytes[content_end - 1u] == '\r')
            content_end--;
        line = (const char *)bytes + line_start;
        line_length = content_end - line_start;
        if (line_length != 0u && line[0] == '#')
        {
            size_t uri_start;
            size_t uri_end;

            if (hls_find_uri_attribute(line, line_length, &uri_start,
                                       &uri_end))
            {
                char uri[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
                size_t media_index;
                size_t uri_length = uri_end - uri_start;

                if (uri_length > AIRPLAY_REMOTE_HLS_URL_MAX)
                    ok = false;
                else
                {
                    memcpy(uri, line + uri_start, uri_length);
                    uri[uri_length] = '\0';
                    if (hls_uri_is_playlist(uri))
                    {
                        ok = hls_register_media_locked(
                                 hls, hls->master_source, uri,
                                 &media_index) &&
                             hls_buffer_append(&output, line, uri_start) &&
                             hls_append_local_media_uri(&output,
                                                        media_index) &&
                             hls_buffer_append(&output, line + uri_end,
                                               line_length - uri_end);
                    }
                    else
                        ok = hls_buffer_append(&output, line, line_length);
                }
            }
            else
                ok = hls_buffer_append(&output, line, line_length);
        }
        else if (line_length != 0u)
        {
            char uri[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
            size_t media_index;

            if (line_length > AIRPLAY_REMOTE_HLS_URL_MAX)
                ok = false;
            else
            {
                memcpy(uri, line, line_length);
                uri[line_length] = '\0';
                if (hls_uri_is_playlist(uri))
                    ok = hls_register_media_locked(
                             hls, hls->master_source, uri, &media_index) &&
                         hls_append_local_media_uri(&output, media_index);
                else
                    ok = hls_buffer_append(&output, line, line_length);
            }
        }
        if (ok)
            ok = hls_buffer_append(&output, "\n", 1u);
    }
    if (!ok)
    {
        hls_buffer_clear(&output);
        return false;
    }
    *playlist_out = (uint8_t *)output.data;
    *playlist_length_out = output.length;
    return true;
}

static bool hls_parse_quoted_attribute(const char *line, const char *name,
                                       char *output, size_t output_size)
{
    char pattern[32];
    const char *start;
    const char *end;
    int written = snprintf(pattern, sizeof(pattern), "%s=\"", name);

    if (written <= 0 || (size_t)written >= sizeof(pattern))
        return false;
    start = strstr(line, pattern);
    if (!start)
        return false;
    start += (size_t)written;
    end = strchr(start, '\"');
    return end && end > start && (size_t)(end - start) < output_size &&
           (memcpy(output, start, (size_t)(end - start)),
            output[end - start] = '\0', true);
}

static bool hls_expand_condensed(const uint8_t *bytes, size_t length,
                                 uint8_t **expanded_out,
                                 size_t *expanded_length_out)
{
    static const char marker[] = "#YT-EXT-CONDENSED-URL";
    AirPlayRemoteHlsBuffer output = {
        .limit = AIRPLAY_REMOTE_HLS_MAX_REWRITTEN_BYTES};
    char *copy;
    char *header;
    char base[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
    char params[1024];
    char prefix[1024];
    char *save = NULL;
    char *line;
    bool condensed = false;
    bool ok = true;

    *expanded_out = NULL;
    *expanded_length_out = 0u;
    copy = malloc(length + 1u);
    if (!copy)
        return false;
    memcpy(copy, bytes, length);
    copy[length] = '\0';
    header = strstr(copy, marker);
    if (header)
    {
        char *header_end = strchr(header, '\n');

        if (header_end)
            *header_end = '\0';
        condensed = hls_parse_quoted_attribute(header, "BASE-URI", base,
                                               sizeof(base)) &&
                    hls_parse_quoted_attribute(header, "PARAMS", params,
                                               sizeof(params)) &&
                    hls_parse_quoted_attribute(header, "PREFIX", prefix,
                                               sizeof(prefix));
        if (header_end)
            *header_end = '\n';
        if (!condensed)
        {
            free(copy);
            return false;
        }
    }
    if (!condensed)
    {
        *expanded_out = (uint8_t *)copy;
        *expanded_length_out = length;
        return true;
    }

    line = strtok_r(copy, "\n", &save);
    while (line && ok)
    {
        size_t line_length = strlen(line);

        if (line_length != 0u && line[line_length - 1u] == '\r')
            line[--line_length] = '\0';
        if (line_length != 0u && line[0] != '#')
        {
            const char *prefix_at = strstr(line, prefix);

            if (prefix_at)
            {
                char values[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
                char params_copy[sizeof(params)];
                char *value_save = NULL;
                char *param_save = NULL;
                char *value;
                char *param;

                ok = hls_buffer_append(&output, line,
                                       (size_t)(prefix_at - line)) &&
                     hls_buffer_append_text(&output, base) &&
                     hls_copy(values, sizeof(values),
                              prefix_at + strlen(prefix)) &&
                     hls_copy(params_copy, sizeof(params_copy), params);
                value = ok ? strtok_r(values, "/", &value_save) : NULL;
                param = ok ? strtok_r(params_copy, ",", &param_save) : NULL;
                while (ok && value && param)
                {
                    ok = hls_buffer_append_text(&output, "/") &&
                         hls_buffer_append_text(&output, param) &&
                         hls_buffer_append_text(&output, "/") &&
                         hls_buffer_append_text(&output, value);
                    value = strtok_r(NULL, "/", &value_save);
                    param = strtok_r(NULL, ",", &param_save);
                }
                if (value || param)
                    ok = false;
            }
            else
                ok = hls_buffer_append(&output, line, line_length);
        }
        else
            ok = hls_buffer_append(&output, line, line_length);
        if (ok)
            ok = hls_buffer_append(&output, "\n", 1u);
        line = strtok_r(NULL, "\n", &save);
    }
    free(copy);
    if (!ok)
    {
        hls_buffer_clear(&output);
        return false;
    }
    *expanded_out = (uint8_t *)output.data;
    *expanded_length_out = output.length;
    return true;
}

static bool hls_append_resolved_media_uri(AirPlayRemoteHlsBuffer *output,
                                          const char *base,
                                          const char *uri)
{
    char resolved[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];

    if (hls_resolve_uri(base, uri, resolved) && hls_is_http_url(resolved))
        return hls_buffer_append_text(output, resolved);
    return hls_buffer_append_text(output, uri);
}

static bool hls_rewrite_media(const char *source_url,
                              const uint8_t *bytes, size_t length,
                              uint8_t **playlist_out,
                              size_t *playlist_length_out)
{
    AirPlayRemoteHlsBuffer output = {
        .limit = AIRPLAY_REMOTE_HLS_MAX_REWRITTEN_BYTES};
    uint8_t *expanded = NULL;
    size_t expanded_length = 0u;
    size_t offset = 0u;
    bool ok = true;

    if (!hls_expand_condensed(bytes, length, &expanded, &expanded_length))
        return false;
    while (offset < expanded_length && ok)
    {
        size_t line_start = offset;
        size_t line_end;
        size_t content_end;
        const char *line;
        size_t line_length;

        while (offset < expanded_length && expanded[offset] != '\n')
            offset++;
        line_end = offset;
        if (offset < expanded_length)
            offset++;
        content_end = line_end;
        if (content_end > line_start && expanded[content_end - 1u] == '\r')
            content_end--;
        line = (const char *)expanded + line_start;
        line_length = content_end - line_start;
        if (line_length != 0u && line[0] == '#')
        {
            size_t uri_start;
            size_t uri_end;

            if (hls_find_uri_attribute(line, line_length, &uri_start,
                                       &uri_end))
            {
                char uri[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];
                size_t uri_length = uri_end - uri_start;

                if (uri_length > AIRPLAY_REMOTE_HLS_URL_MAX)
                    ok = false;
                else
                {
                    memcpy(uri, line + uri_start, uri_length);
                    uri[uri_length] = '\0';
                    ok = hls_buffer_append(&output, line, uri_start) &&
                         hls_append_resolved_media_uri(&output, source_url,
                                                       uri) &&
                         hls_buffer_append(&output, line + uri_end,
                                           line_length - uri_end);
                }
            }
            else
                ok = hls_buffer_append(&output, line, line_length);
        }
        else if (line_length != 0u)
        {
            char uri[AIRPLAY_REMOTE_HLS_URL_MAX + 1u];

            if (line_length > AIRPLAY_REMOTE_HLS_URL_MAX)
                ok = false;
            else
            {
                memcpy(uri, line, line_length);
                uri[line_length] = '\0';
                ok = hls_append_resolved_media_uri(&output, source_url, uri);
            }
        }
        if (ok)
            ok = hls_buffer_append(&output, "\n", 1u);
    }
    free(expanded);
    if (!ok)
    {
        hls_buffer_clear(&output);
        return false;
    }
    *playlist_out = (uint8_t *)output.data;
    *playlist_length_out = output.length;
    return true;
}

static bool hls_get_uint(const AirPlayPlistValue *dict, const char *key,
                         uint64_t *value_out, bool required)
{
    const AirPlayPlistValue *value = airplay_plist_dict_get(dict, key);

    if (!value)
        return !required;
    return airplay_plist_get_uint(value, value_out);
}

bool airplay_remote_hls_handle_action(AirPlayRemoteHls *hls,
                                      uint64_t session_id,
                                      const uint8_t *body,
                                      size_t body_length,
                                      AirPlayRemoteHlsAction *action_out)
{
    AirPlayPlistValue *root = NULL;
    const AirPlayPlistValue *params;
    const AirPlayPlistValue *value;
    const char *type;
    const char *url;
    const uint8_t *playlist;
    size_t playlist_length = 0u;
    uint64_t request_id = 0u;
    uint64_t status_code = 200u;
    AirPlayPlistError error;
    bool ok = false;

    if (!hls || session_id == 0u || !body || body_length == 0u ||
        !action_out)
        return false;
    memset(action_out, 0, sizeof(*action_out));
    if (!airplay_plist_decode(body, body_length, &root, &error) ||
        airplay_plist_type(root) != AIRPLAY_PLIST_TYPE_DICT)
        goto cleanup;
    type = airplay_plist_get_string(airplay_plist_dict_get(root, "type"));
    params = airplay_plist_dict_get(root, "params");
    if (!type || airplay_plist_type(params) != AIRPLAY_PLIST_TYPE_DICT)
        goto cleanup;
    if (strcmp(type, "unhandledURLResponse") != 0)
    {
        action_out->kind = AIRPLAY_REMOTE_HLS_ACTION_ACK;
        ok = true;
        goto cleanup;
    }
    if (!hls_get_uint(params, "FCUP_Response_StatusCode", &status_code,
                      false) ||
        !hls_get_uint(params, "FCUP_Response_RequestID", &request_id, true) ||
        request_id == 0u || request_id > UINT32_MAX || status_code < 200u ||
        status_code > 299u)
        goto cleanup;
    value = airplay_plist_dict_get(params, "FCUP_Response_URL");
    url = airplay_plist_get_string(value);
    value = airplay_plist_dict_get(params, "FCUP_Response_Data");
    playlist = airplay_plist_get_data(value, &playlist_length);
    if (!url || !playlist || !hls_playlist_valid(playlist, playlist_length))
        goto cleanup;

    hls_mutex_lock(&hls->mutex);
    if (!hls->active || hls->session_id != session_id ||
        hls->expected_request_id != (uint32_t)request_id ||
        strcmp(hls->expected_url, url) != 0)
    {
        hls_mutex_unlock(&hls->mutex);
        goto cleanup;
    }
    action_out->generation = hls->generation;
    if (hls->expecting_master)
    {
        uint8_t *rewritten = NULL;
        size_t rewritten_length = 0u;

        hls->media_count = 0u;
        if (!hls_rewrite_master_locked(hls, playlist, playlist_length,
                                       &rewritten, &rewritten_length))
        {
            hls_mutex_unlock(&hls->mutex);
            goto cleanup;
        }
        if (hls->media_count == 0u)
        {
            free(rewritten);
            rewritten = NULL;
            rewritten_length = 0u;
            if (!hls_rewrite_media(hls->master_source, playlist,
                                   playlist_length, &rewritten,
                                   &rewritten_length))
            {
                hls_mutex_unlock(&hls->mutex);
                goto cleanup;
            }
        }
        hls->master_playlist = rewritten;
        hls->master_playlist_length = rewritten_length;
        hls->expecting_master = false;
        hls->pending_media_index = 0u;
    }
    else
    {
        AirPlayRemoteHlsMedia *media;

        if (hls->pending_media_index >= hls->media_count)
        {
            hls_mutex_unlock(&hls->mutex);
            goto cleanup;
        }
        media = &hls->media[hls->pending_media_index];
        if (!hls_rewrite_media(media->source_url, playlist, playlist_length,
                               &media->playlist, &media->playlist_length))
        {
            hls_mutex_unlock(&hls->mutex);
            goto cleanup;
        }
        hls->pending_media_index++;
    }
    if (hls->pending_media_index < hls->media_count)
    {
        if (!hls_prepare_event_locked(
                hls, hls->media[hls->pending_media_index].source_url,
                &action_out->event))
        {
            hls_mutex_unlock(&hls->mutex);
            goto cleanup;
        }
        action_out->kind = AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT;
    }
    else
    {
        if (!hls_copy(action_out->playback_url,
                      sizeof(action_out->playback_url), hls->playback_url))
        {
            hls_mutex_unlock(&hls->mutex);
            goto cleanup;
        }
        hls->expected_request_id = 0u;
        hls->expected_url[0] = '\0';
        action_out->kind = AIRPLAY_REMOTE_HLS_ACTION_READY;
    }
    hls_mutex_unlock(&hls->mutex);
    ok = true;

cleanup:
    if (!ok)
        airplay_remote_hls_event_clear(&action_out->event);
    airplay_plist_free(root);
    return ok;
}

bool airplay_remote_hls_is_local_uri(const char *uri)
{
    return uri && strncmp(uri, AIRPLAY_REMOTE_HLS_LOCAL_PREFIX,
                          sizeof(AIRPLAY_REMOTE_HLS_LOCAL_PREFIX) - 1u) == 0;
}

bool airplay_remote_hls_serve(AirPlayRemoteHls *hls,
                              const AirPlayRtspRequest *request,
                              AirPlayRtspResponse *response,
                              bool *handled_out)
{
    char expected[AIRPLAY_REMOTE_HLS_LOCAL_URL_MAX + 1u];
    const uint8_t *playlist = NULL;
    size_t playlist_length = 0u;
    size_t index;
    int written;
    bool result;

    if (!hls || !request || !response || !handled_out)
        return false;
    *handled_out = airplay_remote_hls_is_local_uri(request->uri);
    if (!*handled_out)
        return true;
    if (strcmp(request->method, "GET") != 0)
        return airplay_rtsp_response_set_status(response, 405);
    hls_mutex_lock(&hls->mutex);
    if (!hls->active)
    {
        hls_mutex_unlock(&hls->mutex);
        return airplay_rtsp_response_set_status(response, 404);
    }
    written = snprintf(expected, sizeof(expected), "%s%s/master.m3u8",
                       AIRPLAY_REMOTE_HLS_LOCAL_PREFIX, hls->token);
    if (written > 0 && (size_t)written < sizeof(expected) &&
        strcmp(request->uri, expected) == 0)
    {
        playlist = hls->master_playlist;
        playlist_length = hls->master_playlist_length;
    }
    else
    {
        for (index = 0u; index < hls->media_count; ++index)
        {
            written = snprintf(expected, sizeof(expected),
                               "%s%s/media/%zu.m3u8",
                               AIRPLAY_REMOTE_HLS_LOCAL_PREFIX, hls->token,
                               index);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(request->uri, expected) == 0)
            {
                playlist = hls->media[index].playlist;
                playlist_length = hls->media[index].playlist_length;
                break;
            }
        }
    }
    if (!playlist)
    {
        hls_mutex_unlock(&hls->mutex);
        return airplay_rtsp_response_set_status(response, 404);
    }
    result = airplay_rtsp_response_set_body(
                 response, playlist, playlist_length,
                 AIRPLAY_REMOTE_HLS_CONTENT_TYPE) &&
             airplay_rtsp_response_add_header(response, "Cache-Control",
                                               "no-store");
    hls_mutex_unlock(&hls->mutex);
    return result;
}

void airplay_remote_hls_reset(AirPlayRemoteHls *hls,
                              uint64_t session_id,
                              uint32_t generation)
{
    if (!hls)
        return;
    hls_mutex_lock(&hls->mutex);
    if ((session_id == 0u || hls->session_id == session_id) &&
        (generation == 0u || hls->generation == generation))
        hls_reset_locked(hls);
    hls_mutex_unlock(&hls->mutex);
}

void airplay_remote_hls_event_clear(AirPlayRemoteHlsEvent *event)
{
    if (!event)
        return;
    free(event->body);
    memset(event, 0, sizeof(*event));
}

const char *airplay_remote_hls_action_name(AirPlayRemoteHlsActionKind kind)
{
    switch (kind)
    {
    case AIRPLAY_REMOTE_HLS_ACTION_ACK:
        return "ack";
    case AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT:
        return "request-next";
    case AIRPLAY_REMOTE_HLS_ACTION_READY:
        return "ready";
    default:
        return "unknown";
    }
}
