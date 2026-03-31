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

static bool is_bilibili_source(const char *uri, const char *metadata)
{
    return contains_ignore_case(uri, "bilivideo.com") ||
           contains_ignore_case(uri, "bilibili.com") ||
           contains_ignore_case(metadata, "bilivideo") ||
           contains_ignore_case(metadata, "bilibili");
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

bool player_source_resolve(const char *uri, const char *metadata, PlayerResolvedSource *out)
{
    if (!uri || uri[0] == '\0' || !out)
        return false;

    player_source_reset(out);
    snprintf(out->uri, sizeof(out->uri), "%s", uri);
    if (metadata)
        snprintf(out->metadata, sizeof(out->metadata), "%s", metadata);

    out->flags.is_http = is_http_like_uri(uri);
    out->flags.is_https = is_https_uri(uri);
    out->flags.is_hls = is_hls_uri(uri);
    out->flags.is_signed = has_signed_tokens(uri);
    out->flags.is_bilibili = is_bilibili_source(uri, metadata);

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
