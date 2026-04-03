#include "player/ingress/hls.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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

bool ingress_hls_uri_matches(const char *uri)
{
    return contains_ignore_case(uri, ".m3u8") || contains_ignore_case(uri, "format=m3u8");
}

bool ingress_hls_mime_matches(const char *value)
{
    return contains_ignore_case(value, "application/vnd.apple.mpegurl") ||
           contains_ignore_case(value, "application/x-mpegurl");
}

bool ingress_hls_live_hint(const char *uri, const char *metadata)
{
    return contains_ignore_case(uri, "/live/") ||
           contains_ignore_case(uri, "live/") ||
           contains_ignore_case(uri, "stream=live") ||
           contains_ignore_case(uri, "channel") ||
           contains_ignore_case(metadata, " live ") ||
           contains_ignore_case(metadata, "live/") ||
           contains_ignore_case(metadata, "channel");
}

int ingress_hls_default_readahead_seconds(bool live_hint)
{
    return live_hint ? 3 : 6;
}

int ingress_hls_cache_pause_wait_seconds(bool live_hint)
{
    return live_hint ? 1 : 3;
}
