#include "player/trace.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

#define PLAYER_TRACE_URL_SUMMARY_MAX 160

static uint32_t g_next_media_seq = 0;
static uint32_t g_current_media_seq = 0;
static uint32_t g_current_media_hash = 0;

static LogLevel player_trace_normal_level(void)
{
#if defined(NXCAST_MEDIA_TRACE_VERBOSE) && NXCAST_MEDIA_TRACE_VERBOSE
    return LOG_LEVEL_WARN;
#else
    return LOG_LEVEL_INFO;
#endif
}

void player_trace_log(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vlog_write(player_trace_normal_level(), fmt, args);
    va_end(args);
}

void player_trace_warn(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vlog_write(LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

static uint32_t player_trace_next_seq(void)
{
    uint32_t seq = __atomic_add_fetch(&g_next_media_seq, 1u, __ATOMIC_RELAXED);
    if (seq == 0)
        seq = __atomic_add_fetch(&g_next_media_seq, 1u, __ATOMIC_RELAXED);
    return seq;
}

uint32_t player_trace_uri_hash(const char *uri)
{
    uint32_t hash = 2166136261u;

    if (!uri)
        return 0;

    while (*uri)
    {
        hash ^= (unsigned char)*uri++;
        hash *= 16777619u;
    }

    return hash;
}

const char *player_trace_uri_summary(const char *uri, char *buffer, size_t buffer_size)
{
    const char *start;
    const char *end;
    size_t len;
    bool truncated = false;

    if (!buffer || buffer_size == 0)
        return "";

    if (!uri)
    {
        snprintf(buffer, buffer_size, "(null)");
        return buffer;
    }

    start = uri;
    if (strncmp(start, "http://", 7) == 0)
        start += 7;
    else if (strncmp(start, "https://", 8) == 0)
        start += 8;

    end = start;
    while (*end && *end != '?' && *end != '#')
        ++end;

    len = 0;
    while (&start[len] < end && len + 1 < buffer_size)
    {
        buffer[len] = start[len];
        ++len;
    }

    if (&start[len] < end)
        truncated = true;

    if (len + 1 == buffer_size && &start[len] < end)
    {
        truncated = true;
    }
    buffer[len] = '\0';

    if (truncated && buffer_size > 4)
    {
        buffer[buffer_size - 4] = '.';
        buffer[buffer_size - 3] = '.';
        buffer[buffer_size - 2] = '.';
        buffer[buffer_size - 1] = '\0';
    }

    return buffer;
}

uint32_t player_trace_begin_media(const char *reason, const char *uri, const char *metadata)
{
    uint32_t seq = player_trace_next_seq();
    uint32_t hash = player_trace_uri_hash(uri);
    char summary[PLAYER_TRACE_URL_SUMMARY_MAX];

    __atomic_store_n(&g_current_media_seq, seq, __ATOMIC_RELEASE);
    __atomic_store_n(&g_current_media_hash, hash, __ATOMIC_RELEASE);

    player_trace_log("[media-trace] seq=%u action=%s phase=begin url_hash=%08x metadata_bytes=%zu url=%s\n",
                     seq,
                     reason ? reason : "SetMedia",
                     hash,
                     metadata ? strlen(metadata) : 0u,
                     player_trace_uri_summary(uri, summary, sizeof(summary)));
    return seq;
}

uint32_t player_trace_current_media_seq(void)
{
    return __atomic_load_n(&g_current_media_seq, __ATOMIC_ACQUIRE);
}

uint32_t player_trace_current_media_hash(void)
{
    return __atomic_load_n(&g_current_media_hash, __ATOMIC_ACQUIRE);
}
