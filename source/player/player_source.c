#include "player_source.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "source_policy.h"

static bool starts_with_ignore_case(const char *value, const char *prefix)
{
    if (!value || !prefix)
        return false;

    while (*prefix)
    {
        if (*value == '\0')
            return false;
        if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
            return false;
        ++value;
        ++prefix;
    }
    return true;
}

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

static bool is_http_like_uri(const char *uri)
{
    return starts_with_ignore_case(uri, "http://") || starts_with_ignore_case(uri, "https://");
}

static bool is_https_uri(const char *uri)
{
    return starts_with_ignore_case(uri, "https://");
}

static bool has_signed_tokens(const char *uri)
{
    static const char *tokens[] = {
        "token=",
        "sign=",
        "sig=",
        "expires=",
        "deadline=",
        "e=",
        "auth_key=",
        "upsig=",
        "wssecret=",
        "wstime="
    };

    if (!uri)
        return false;

    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i)
    {
        if (contains_ignore_case(uri, tokens[i]))
            return true;
    }

    return false;
}

static bool is_hls_uri(const char *uri)
{
    return contains_ignore_case(uri, ".m3u8");
}

static bool is_dash_uri(const char *uri)
{
    return contains_ignore_case(uri, ".mpd") || contains_ignore_case(uri, ".m4s");
}

static bool is_flv_uri(const char *uri)
{
    return contains_ignore_case(uri, ".flv");
}

static bool is_mp4_uri(const char *uri)
{
    return contains_ignore_case(uri, ".mp4");
}

static bool is_mpeg_ts_uri(const char *uri)
{
    return contains_ignore_case(uri, ".ts");
}

static bool is_bilibili_source(const char *uri, const char *metadata)
{
    return contains_ignore_case(uri, "bilivideo.com") ||
           contains_ignore_case(uri, "bilibili.com") ||
           contains_ignore_case(metadata, "bilivideo") ||
           contains_ignore_case(metadata, "bilibili");
}

static void set_if_contains(char *out, size_t out_size, const char *metadata, const char *needle, const char *value)
{
    if (!out || out_size == 0 || out[0] != '\0')
        return;
    if (contains_ignore_case(metadata, needle))
        snprintf(out, out_size, "%s", value);
}

static void detect_metadata_mime(const char *metadata, char *mime_type, size_t mime_type_size)
{
    if (!mime_type || mime_type_size == 0)
        return;

    mime_type[0] = '\0';
    if (!metadata || metadata[0] == '\0')
        return;

    set_if_contains(mime_type, mime_type_size, metadata, "application/vnd.apple.mpegurl", "application/vnd.apple.mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "application/x-mpegurl", "application/x-mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "application/dash+xml", "application/dash+xml");
    set_if_contains(mime_type, mime_type_size, metadata, "video/x-flv", "video/x-flv");
    set_if_contains(mime_type, mime_type_size, metadata, "video/mp4", "video/mp4");
    set_if_contains(mime_type, mime_type_size, metadata, "video/vnd.dlna.mpeg-tts", "video/vnd.dlna.mpeg-tts");
    set_if_contains(mime_type, mime_type_size, metadata, "video/mp2t", "video/mp2t");
}

static PlayerSourceFormat detect_source_format(const char *uri, const char *metadata, bool *likely_segmented)
{
    bool segmented = false;

    if (contains_ignore_case(metadata, "application/vnd.apple.mpegurl") ||
        contains_ignore_case(metadata, "application/x-mpegurl") ||
        is_hls_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_SOURCE_FORMAT_HLS;
    }

    if (contains_ignore_case(metadata, "application/dash+xml") || is_dash_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = true;
        return PLAYER_SOURCE_FORMAT_DASH;
    }

    if (contains_ignore_case(metadata, "video/x-flv") || is_flv_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_SOURCE_FORMAT_FLV;
    }

    if (contains_ignore_case(metadata, "video/vnd.dlna.mpeg-tts") ||
        contains_ignore_case(metadata, "video/mp2t") ||
        is_mpeg_ts_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_SOURCE_FORMAT_MPEG_TS;
    }

    if (contains_ignore_case(metadata, "video/mp4") || is_mp4_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_SOURCE_FORMAT_MP4;
    }

    if (likely_segmented)
        *likely_segmented = segmented;
    return PLAYER_SOURCE_FORMAT_UNKNOWN;
}

void player_source_reset(PlayerResolvedSource *source)
{
    if (!source)
        return;

    memset(source, 0, sizeof(*source));
    source->profile = PLAYER_SOURCE_PROFILE_UNKNOWN;
    source_policy_apply_default(source);
}

const char *player_source_profile_name(PlayerSourceProfile profile)
{
    switch (profile)
    {
    case PLAYER_SOURCE_PROFILE_DIRECT_HTTP_FILE:
        return "direct-http-file";
    case PLAYER_SOURCE_PROFILE_GENERIC_HLS:
        return "generic-hls";
    case PLAYER_SOURCE_PROFILE_HEADER_SENSITIVE_HTTP:
        return "header-sensitive-http";
    case PLAYER_SOURCE_PROFILE_SIGNED_EPHEMERAL_URL:
        return "signed-ephemeral-url";
    case PLAYER_SOURCE_PROFILE_VENDOR_SENSITIVE_URL:
        return "vendor-sensitive-url";
    case PLAYER_SOURCE_PROFILE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *player_source_format_name(PlayerSourceFormat format)
{
    switch (format)
    {
    case PLAYER_SOURCE_FORMAT_MP4:
        return "mp4";
    case PLAYER_SOURCE_FORMAT_FLV:
        return "flv";
    case PLAYER_SOURCE_FORMAT_HLS:
        return "hls";
    case PLAYER_SOURCE_FORMAT_DASH:
        return "dash";
    case PLAYER_SOURCE_FORMAT_MPEG_TS:
        return "mpeg-ts";
    case PLAYER_SOURCE_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

bool player_source_resolve(const char *uri, const char *metadata, PlayerResolvedSource *out)
{
    bool likely_segmented = false;

    if (!uri || uri[0] == '\0' || !out)
        return false;

    player_source_reset(out);
    snprintf(out->uri, sizeof(out->uri), "%s", uri);
    if (metadata)
        snprintf(out->metadata, sizeof(out->metadata), "%s", metadata);

    detect_metadata_mime(metadata, out->mime_type, sizeof(out->mime_type));
    out->format = detect_source_format(uri, metadata, &likely_segmented);

    out->flags.is_http = is_http_like_uri(uri);
    out->flags.is_https = is_https_uri(uri);
    out->flags.is_hls = out->format == PLAYER_SOURCE_FORMAT_HLS;
    out->flags.is_signed = has_signed_tokens(uri);
    out->flags.is_bilibili = is_bilibili_source(uri, metadata);
    out->flags.is_dash = out->format == PLAYER_SOURCE_FORMAT_DASH;
    out->flags.is_flv = out->format == PLAYER_SOURCE_FORMAT_FLV;
    out->flags.is_mp4 = out->format == PLAYER_SOURCE_FORMAT_MP4;
    out->flags.is_mpeg_ts = out->format == PLAYER_SOURCE_FORMAT_MPEG_TS;
    out->flags.likely_segmented = likely_segmented;
    out->flags.likely_video_only = out->flags.is_dash && out->flags.is_bilibili && likely_segmented;

    snprintf(out->format_hint, sizeof(out->format_hint), "%s", player_source_format_name(out->format));

    if (out->flags.is_hls)
        out->profile = PLAYER_SOURCE_PROFILE_GENERIC_HLS;
    else if (out->flags.is_signed)
        out->profile = PLAYER_SOURCE_PROFILE_SIGNED_EPHEMERAL_URL;
    else if (out->flags.is_http)
        out->profile = PLAYER_SOURCE_PROFILE_DIRECT_HTTP_FILE;

    source_policy_apply_hls(out);
    source_policy_apply_vendor(out);

    return true;
}
