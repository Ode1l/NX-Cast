#include "player/ingress/evidence.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "player/ingress/vendor.h"

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

static void set_if_contains(char *out, size_t out_size, const char *metadata, const char *needle, const char *value)
{
    if (!out || out_size == 0 || out[0] != '\0')
        return;
    if (contains_ignore_case(metadata, needle))
        snprintf(out, out_size, "%s", value);
}

bool ingress_evidence_is_http_like_uri(const char *uri)
{
    return starts_with_ignore_case(uri, "http://") || starts_with_ignore_case(uri, "https://");
}

bool ingress_evidence_is_https_uri(const char *uri)
{
    return starts_with_ignore_case(uri, "https://");
}

bool ingress_evidence_uri_host_is_ipv4_literal(const char *uri)
{
    const char *host;
    const char *end;
    int dot_count = 0;

    if (!ingress_evidence_is_http_like_uri(uri))
        return false;

    host = strstr(uri, "://");
    if (!host)
        return false;
    host += 3;
    end = host;
    while (*end && *end != ':' && *end != '/' && *end != '?' && *end != '#')
        ++end;
    if (end == host)
        return false;

    for (const char *cursor = host; cursor < end; ++cursor)
    {
        if (*cursor == '.')
        {
            ++dot_count;
            continue;
        }
        if (!isdigit((unsigned char)*cursor))
            return false;
    }

    return dot_count == 3;
}

bool ingress_evidence_has_signed_tokens(const char *uri)
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

void ingress_evidence_detect_metadata_mime(const char *metadata, char *mime_type, size_t mime_type_size)
{
    if (!mime_type || mime_type_size == 0)
        return;

    mime_type[0] = '\0';
    if (!metadata || metadata[0] == '\0')
        return;

    set_if_contains(mime_type, mime_type_size, metadata, "application/vnd.apple.mpegurl", "application/vnd.apple.mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "application/x-mpegurl", "application/x-mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "application/mpegurl", "application/mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "audio/mpegurl", "audio/mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "audio/x-mpegurl", "audio/x-mpegurl");
    set_if_contains(mime_type, mime_type_size, metadata, "application/dash+xml", "application/dash+xml");
    set_if_contains(mime_type, mime_type_size, metadata, "video/x-flv", "video/x-flv");
    set_if_contains(mime_type, mime_type_size, metadata, "video/mp4", "video/mp4");
    set_if_contains(mime_type, mime_type_size, metadata, "application/mp4", "application/mp4");
    set_if_contains(mime_type, mime_type_size, metadata, "video/vnd.dlna.mpeg-tts", "video/vnd.dlna.mpeg-tts");
    set_if_contains(mime_type, mime_type_size, metadata, "video/mp2t", "video/mp2t");
}

void ingress_collect_evidence(const char *uri, const char *metadata, const PlayerOpenContext *ctx, IngressEvidence *out)
{
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->input_uri = uri;
    out->metadata = metadata;
    out->input_is_http = ingress_evidence_is_http_like_uri(uri);
    out->input_is_https = ingress_evidence_is_https_uri(uri);
    out->input_is_signed = ingress_evidence_has_signed_tokens(uri);
    out->input_is_bare_ipv4 = ingress_evidence_uri_host_is_ipv4_literal(uri);
    ingress_evidence_detect_metadata_mime(metadata, out->metadata_mime, sizeof(out->metadata_mime));

    if (ctx)
        out->sender_vendor = ingress_detect_vendor_from_sender_ua(ctx->sender_user_agent);
}
