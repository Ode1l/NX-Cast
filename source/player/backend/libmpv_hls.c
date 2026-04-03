#include "player/backend/libmpv_hls.h"

#ifdef HAVE_LIBMPV

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "log/log.h"

static bool contains_ignore_case(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return false;

    needle_len = strlen(needle);
    if (needle_len == 0)
        return true;

    for (const char *cursor = haystack; *cursor; ++cursor)
    {
        size_t i = 0;
        while (i < needle_len &&
               cursor[i] &&
               tolower((unsigned char)cursor[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if (i == needle_len)
            return true;
    }

    return false;
}

const char *libmpv_hls_runtime_kind_name(LibmpvHlsRuntimeKind kind)
{
    switch (kind)
    {
    case LIBMPV_HLS_RUNTIME_VOD:
        return "vod";
    case LIBMPV_HLS_RUNTIME_LIVE:
        return "live";
    case LIBMPV_HLS_RUNTIME_UNKNOWN:
    default:
        return "unknown";
    }
}

LibmpvHlsRuntimeKind libmpv_hls_detect_runtime_kind(bool live_hint,
                                                    bool media_loaded,
                                                    bool seekable,
                                                    int duration_ms,
                                                    const char *stream_path,
                                                    const char *demuxer)
{
    if (!media_loaded)
        return live_hint ? LIBMPV_HLS_RUNTIME_LIVE : LIBMPV_HLS_RUNTIME_UNKNOWN;

    if (seekable && duration_ms > 0)
        return LIBMPV_HLS_RUNTIME_VOD;

    if (!seekable && duration_ms <= 0)
        return LIBMPV_HLS_RUNTIME_LIVE;

    if (contains_ignore_case(stream_path, "/live/") ||
        contains_ignore_case(stream_path, "live/") ||
        contains_ignore_case(stream_path, "channel"))
    {
        return LIBMPV_HLS_RUNTIME_LIVE;
    }

    if (contains_ignore_case(demuxer, "hls") && live_hint && !seekable)
        return LIBMPV_HLS_RUNTIME_LIVE;

    return live_hint ? LIBMPV_HLS_RUNTIME_LIVE : LIBMPV_HLS_RUNTIME_UNKNOWN;
}

void libmpv_hls_log_cache_state_node(const mpv_node *node)
{
    if (!node || node->format != MPV_FORMAT_NODE_MAP || !node->u.list)
        return;

    const mpv_node_list *list = node->u.list;
    int64_t total_bytes = 0;
    int64_t fw_bytes = 0;
    int64_t raw_input_rate = 0;
    double cache_duration = 0.0;
    bool underrun = false;
    bool bof_cached = false;
    bool eof_cached = false;

    for (int i = 0; i < list->num; ++i)
    {
        const char *key = list->keys ? list->keys[i] : NULL;
        const mpv_node *value = &list->values[i];
        if (!key || !value)
            continue;

        if (strcmp(key, "total-bytes") == 0 && value->format == MPV_FORMAT_INT64)
            total_bytes = value->u.int64;
        else if (strcmp(key, "fw-bytes") == 0 && value->format == MPV_FORMAT_INT64)
            fw_bytes = value->u.int64;
        else if (strcmp(key, "raw-input-rate") == 0 && value->format == MPV_FORMAT_INT64)
            raw_input_rate = value->u.int64;
        else if (strcmp(key, "cache-duration") == 0 && value->format == MPV_FORMAT_DOUBLE)
            cache_duration = value->u.double_;
        else if (strcmp(key, "underrun") == 0 && value->format == MPV_FORMAT_FLAG)
            underrun = value->u.flag != 0;
        else if (strcmp(key, "bof-cached") == 0 && value->format == MPV_FORMAT_FLAG)
            bof_cached = value->u.flag != 0;
        else if (strcmp(key, "eof-cached") == 0 && value->format == MPV_FORMAT_FLAG)
            eof_cached = value->u.flag != 0;
    }

    log_info("[player-libmpv] cache_state total_mb=%.2f fw_mb=%.2f cache_sec=%.2f raw_rate_bps=%lld underrun=%d bof=%d eof=%d\n",
             total_bytes / 1048576.0,
             fw_bytes / 1048576.0,
             cache_duration,
             (long long)raw_input_rate,
             underrun ? 1 : 0,
             bof_cached ? 1 : 0,
             eof_cached ? 1 : 0);
}

#endif
