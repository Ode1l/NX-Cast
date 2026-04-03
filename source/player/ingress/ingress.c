#include "player/ingress.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "player/ingress/hls.h"
#include "player/ingress/vendor.h"
#include "player/policy.h"

#define PLAYER_MEDIA_METADATA_RESOURCE_MAX 8

typedef struct
{
    char uri[PLAYER_MEDIA_URI_MAX];
    char protocol_info[PLAYER_MEDIA_PROTOCOL_INFO_MAX];
    char mime_type[PLAYER_MEDIA_MIME_TYPE_MAX];
    PlayerMediaFormat format;
    bool likely_segmented;
    bool exact_uri_match;
    int score;
} PlayerMetadataResource;

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

static bool span_equals_ignore_case(const char *start, const char *end, const char *value)
{
    size_t span_len;
    size_t value_len;

    if (!start || !end || !value || end < start)
        return false;

    span_len = (size_t)(end - start);
    value_len = strlen(value);
    return span_len == value_len && strncasecmp(start, value, value_len) == 0;
}

static bool local_name_equals_ignore_case(const char *start, const char *end, const char *value)
{
    const char *colon;

    if (!start || !end || end < start)
        return false;

    colon = memchr(start, ':', (size_t)(end - start));
    if (colon && colon + 1 < end)
        start = colon + 1;

    return span_equals_ignore_case(start, end, value);
}

static void trim_ascii_in_place(char *value)
{
    size_t begin = 0;
    size_t end;

    if (!value || value[0] == '\0')
        return;

    while (value[begin] && isspace((unsigned char)value[begin]))
        ++begin;
    if (begin > 0)
        memmove(value, value + begin, strlen(value + begin) + 1);

    end = strlen(value);
    while (end > 0 && isspace((unsigned char)value[end - 1]))
        value[--end] = '\0';
}

static void xml_decode_span(const char *start, const char *end, char *out, size_t out_size)
{
    size_t length = 0;

    if (!out || out_size == 0)
        return;

    out[0] = '\0';
    if (!start || !end || end < start)
        return;

    while (start < end && length + 1 < out_size)
    {
        if (*start == '&')
        {
            size_t remaining = (size_t)(end - start);
            const char *replacement = NULL;
            size_t consumed = 0;

            if (remaining >= 5 && strncmp(start, "&amp;", 5) == 0)
            {
                replacement = "&";
                consumed = 5;
            }
            else if (remaining >= 4 && strncmp(start, "&lt;", 4) == 0)
            {
                replacement = "<";
                consumed = 4;
            }
            else if (remaining >= 4 && strncmp(start, "&gt;", 4) == 0)
            {
                replacement = ">";
                consumed = 4;
            }
            else if (remaining >= 6 && strncmp(start, "&quot;", 6) == 0)
            {
                replacement = "\"";
                consumed = 6;
            }
            else if (remaining >= 6 && strncmp(start, "&apos;", 6) == 0)
            {
                replacement = "'";
                consumed = 6;
            }

            if (replacement)
            {
                out[length++] = replacement[0];
                start += consumed;
                continue;
            }
        }

        out[length++] = *start++;
    }

    out[length] = '\0';
    trim_ascii_in_place(out);
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

static void extract_protocol_info_mime(const char *protocol_info, char *mime_type, size_t mime_type_size)
{
    const char *first;
    const char *second;
    const char *third;

    if (!mime_type || mime_type_size == 0)
        return;

    mime_type[0] = '\0';
    if (!protocol_info || protocol_info[0] == '\0')
        return;

    first = strchr(protocol_info, ':');
    if (!first)
        return;
    second = strchr(first + 1, ':');
    if (!second)
        return;
    third = strchr(second + 1, ':');
    if (!third || third <= second + 1)
        return;

    size_t copy_len = (size_t)(third - (second + 1));
    if (copy_len >= mime_type_size)
        copy_len = mime_type_size - 1;

    memcpy(mime_type, second + 1, copy_len);
    mime_type[copy_len] = '\0';
    trim_ascii_in_place(mime_type);
}

static PlayerMediaFormat detect_media_format(const char *uri, const char *metadata_or_mime, bool *likely_segmented)
{
    bool segmented = false;

    if (ingress_hls_mime_matches(metadata_or_mime) || ingress_hls_uri_matches(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_HLS;
    }

    if (contains_ignore_case(metadata_or_mime, "application/dash+xml") || is_dash_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = true;
        return PLAYER_MEDIA_FORMAT_DASH;
    }

    if (contains_ignore_case(metadata_or_mime, "video/x-flv") || is_flv_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_FLV;
    }

    if (contains_ignore_case(metadata_or_mime, "video/vnd.dlna.mpeg-tts") ||
        contains_ignore_case(metadata_or_mime, "video/mp2t") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=avc_ts") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=mpeg_ts") ||
        is_mpeg_ts_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_MPEG_TS;
    }

    if (contains_ignore_case(metadata_or_mime, "video/mp4") ||
        contains_ignore_case(metadata_or_mime, "application/mp4") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=avc_mp4") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=mpeg4") ||
        is_mp4_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_MP4;
    }

    if (likely_segmented)
        *likely_segmented = segmented;
    return PLAYER_MEDIA_FORMAT_UNKNOWN;
}

static bool extract_xml_attribute_value(const char *tag_start, const char *tag_end, const char *attr_name,
                                        char *out, size_t out_size)
{
    const char *cursor;

    if (!tag_start || !tag_end || !attr_name || !out || out_size == 0 || tag_end < tag_start)
        return false;

    out[0] = '\0';
    cursor = tag_start + 1;
    while (cursor < tag_end)
    {
        while (cursor < tag_end && isspace((unsigned char)*cursor))
            ++cursor;
        while (cursor < tag_end && *cursor != '>' && !isspace((unsigned char)*cursor))
            ++cursor;
        while (cursor < tag_end && isspace((unsigned char)*cursor))
            ++cursor;
        if (cursor >= tag_end || *cursor == '>' || *cursor == '/')
            break;

        const char *name_start = cursor;
        while (cursor < tag_end && *cursor != '=' && !isspace((unsigned char)*cursor) && *cursor != '>')
            ++cursor;
        const char *name_end = cursor;

        while (cursor < tag_end && isspace((unsigned char)*cursor))
            ++cursor;
        if (cursor >= tag_end || *cursor != '=')
        {
            while (cursor < tag_end && *cursor != '>' && !isspace((unsigned char)*cursor))
                ++cursor;
            continue;
        }
        ++cursor;
        while (cursor < tag_end && isspace((unsigned char)*cursor))
            ++cursor;
        if (cursor >= tag_end || (*cursor != '"' && *cursor != '\''))
            continue;

        char quote = *cursor++;
        const char *value_start = cursor;
        while (cursor < tag_end && *cursor != quote)
            ++cursor;
        const char *value_end = cursor;
        if (cursor < tag_end)
            ++cursor;

        if (local_name_equals_ignore_case(name_start, name_end, attr_name))
        {
            xml_decode_span(value_start, value_end, out, out_size);
            return out[0] != '\0';
        }
    }

    return false;
}

static int score_metadata_resource(const char *current_uri, const PlayerMetadataResource *resource,
                                   PlayerMediaVendor vendor)
{
    int score = 0;

    if (!resource || resource->uri[0] == '\0')
        return -100000;

    if (is_http_like_uri(resource->uri))
        score += 50;
    else
        score -= 50;

    if (starts_with_ignore_case(resource->protocol_info, "http-get:"))
        score += 120;
    else if (resource->protocol_info[0] != '\0')
        score -= 120;

    if (resource->mime_type[0] != '\0')
        score += 25;

    switch (resource->format)
    {
    case PLAYER_MEDIA_FORMAT_MP4:
        score += 500;
        break;
    case PLAYER_MEDIA_FORMAT_HLS:
        score += 450;
        break;
    case PLAYER_MEDIA_FORMAT_MPEG_TS:
        score += 420;
        break;
    case PLAYER_MEDIA_FORMAT_FLV:
        score += 320;
        break;
    case PLAYER_MEDIA_FORMAT_UNKNOWN:
        score += 180;
        break;
    case PLAYER_MEDIA_FORMAT_DASH:
        score += 40;
        break;
    }

    if (resource->likely_segmented)
        score -= 40;
    if (resource->exact_uri_match)
        score += 10;
    if (current_uri && current_uri[0] != '\0' && strcmp(resource->uri, current_uri) == 0)
        score += 10;

    if (ingress_vendor_is_sensitive(vendor))
    {
        switch (resource->format)
        {
        case PLAYER_MEDIA_FORMAT_HLS:
        case PLAYER_MEDIA_FORMAT_FLV:
        case PLAYER_MEDIA_FORMAT_MPEG_TS:
            score += 50;
            break;
        case PLAYER_MEDIA_FORMAT_MP4:
            score += 20;
            break;
        case PLAYER_MEDIA_FORMAT_DASH:
            score -= 120;
            break;
        case PLAYER_MEDIA_FORMAT_UNKNOWN:
        default:
            break;
        }
    }

    return score;
}

static int parse_metadata_resources(const char *metadata, const char *current_uri,
                                    PlayerMetadataResource *resources, int max_resources,
                                    PlayerMediaVendor vendor)
{
    const char *cursor;
    int count = 0;

    if (!metadata || metadata[0] == '\0' || !resources || max_resources <= 0)
        return 0;

    cursor = metadata;
    while ((cursor = strchr(cursor, '<')) != NULL)
    {
        if (cursor[1] == '/' || cursor[1] == '?' || cursor[1] == '!')
        {
            ++cursor;
            continue;
        }

        const char *name_start = cursor + 1;
        while (*name_start && isspace((unsigned char)*name_start))
            ++name_start;

        const char *name_end = name_start;
        while (*name_end && !isspace((unsigned char)*name_end) && *name_end != '>' && *name_end != '/')
            ++name_end;
        if (name_end == name_start)
        {
            ++cursor;
            continue;
        }

        const char *open_end = strchr(name_end, '>');
        if (!open_end)
            break;

        if (!local_name_equals_ignore_case(name_start, name_end, "res"))
        {
            cursor = open_end + 1;
            continue;
        }

        if (open_end > cursor && open_end[-1] == '/')
        {
            cursor = open_end + 1;
            continue;
        }

        const char *value_start = open_end + 1;
        const char *scan = value_start;
        const char *close = NULL;

        while (true)
        {
            close = strstr(scan, "</");
            if (!close)
                break;

            const char *close_name_start = close + 2;
            while (*close_name_start && isspace((unsigned char)*close_name_start))
                ++close_name_start;
            const char *close_name_end = close_name_start;
            while (*close_name_end && !isspace((unsigned char)*close_name_end) && *close_name_end != '>')
                ++close_name_end;

            if (local_name_equals_ignore_case(close_name_start, close_name_end, "res"))
                break;

            scan = close + 2;
        }

        if (!close)
            break;

        if (count < max_resources)
        {
            PlayerMetadataResource *resource = &resources[count];
            memset(resource, 0, sizeof(*resource));

            extract_xml_attribute_value(cursor, open_end, "protocolInfo",
                                        resource->protocol_info, sizeof(resource->protocol_info));
            extract_protocol_info_mime(resource->protocol_info, resource->mime_type, sizeof(resource->mime_type));
            xml_decode_span(value_start, close, resource->uri, sizeof(resource->uri));

            if (resource->uri[0] != '\0')
            {
                resource->exact_uri_match = current_uri && strcmp(resource->uri, current_uri) == 0;
                resource->format = detect_media_format(resource->uri,
                                                       resource->mime_type[0] != '\0' ? resource->mime_type : resource->protocol_info,
                                                       &resource->likely_segmented);
                resource->score = score_metadata_resource(current_uri, resource, vendor);
                ++count;
            }
        }

        cursor = close + 2;
    }

    return count;
}

static void populate_media_flags(PlayerMedia *media, bool likely_segmented)
{
    const char *metadata = media->metadata[0] != '\0' ? media->metadata : NULL;
    const char *resolved_uri = media->uri[0] != '\0' ? media->uri : media->original_uri;

    media->flags.is_http = is_http_like_uri(media->uri);
    media->flags.is_https = is_https_uri(media->uri);
    media->flags.is_hls = media->format == PLAYER_MEDIA_FORMAT_HLS;
    media->flags.likely_live = media->flags.is_hls &&
                               ingress_hls_live_hint(resolved_uri, metadata);
    media->flags.is_signed = has_signed_tokens(media->uri);
    media->vendor = ingress_detect_vendor(resolved_uri, metadata);
    if (media->vendor == PLAYER_MEDIA_VENDOR_UNKNOWN && media->original_uri[0] != '\0')
        media->vendor = ingress_detect_vendor(media->original_uri, metadata);
    media->flags.is_bilibili = media->vendor == PLAYER_MEDIA_VENDOR_BILIBILI;
    media->flags.is_dash = media->format == PLAYER_MEDIA_FORMAT_DASH;
    media->flags.is_flv = media->format == PLAYER_MEDIA_FORMAT_FLV;
    media->flags.is_mp4 = media->format == PLAYER_MEDIA_FORMAT_MP4;
    media->flags.is_mpeg_ts = media->format == PLAYER_MEDIA_FORMAT_MPEG_TS;
    media->flags.likely_segmented = likely_segmented;
    media->flags.likely_video_only = media->flags.is_dash &&
                                     likely_segmented &&
                                     ingress_vendor_is_sensitive(media->vendor);
}

static void select_metadata_resource_if_better(const char *input_uri, PlayerMedia *out, bool *likely_segmented,
                                               PlayerMediaVendor vendor)
{
    PlayerMetadataResource resources[PLAYER_MEDIA_METADATA_RESOURCE_MAX];
    PlayerMetadataResource baseline;
    int resource_count;
    int best_index = -1;
    int best_score = -100000;
    int baseline_score;

    if (!out || !likely_segmented)
        return;

    resource_count = parse_metadata_resources(out->metadata, input_uri, resources, PLAYER_MEDIA_METADATA_RESOURCE_MAX, vendor);
    out->metadata_candidate_count = resource_count;
    if (resource_count <= 0)
        return;

    memset(&baseline, 0, sizeof(baseline));
    snprintf(baseline.uri, sizeof(baseline.uri), "%s", input_uri ? input_uri : "");
    snprintf(baseline.mime_type, sizeof(baseline.mime_type), "%s", out->mime_type);
    baseline.format = out->format;
    baseline.likely_segmented = *likely_segmented;
    baseline.exact_uri_match = true;
    baseline.score = score_metadata_resource(input_uri, &baseline, vendor);
    baseline_score = baseline.score;

    for (int i = 0; i < resource_count; ++i)
    {
        if (resources[i].score > best_score)
        {
            best_score = resources[i].score;
            best_index = i;
        }
    }

    if (best_index < 0)
        return;

    if (best_score < baseline_score && !resources[best_index].exact_uri_match)
        return;

    snprintf(out->uri, sizeof(out->uri), "%s", resources[best_index].uri);
    snprintf(out->protocol_info, sizeof(out->protocol_info), "%s", resources[best_index].protocol_info);
    if (resources[best_index].mime_type[0] != '\0')
        snprintf(out->mime_type, sizeof(out->mime_type), "%s", resources[best_index].mime_type);
    out->format = resources[best_index].format;
    out->selected_from_metadata = true;
    *likely_segmented = resources[best_index].likely_segmented;
}

void ingress_reset(PlayerMedia *media)
{
    if (!media)
        return;

    memset(media, 0, sizeof(*media));
    media->profile = PLAYER_MEDIA_PROFILE_UNKNOWN;
    policy_apply_default(media);
}

const char *ingress_profile_name(PlayerMediaProfile profile)
{
    switch (profile)
    {
    case PLAYER_MEDIA_PROFILE_DIRECT_HTTP_FILE:
        return "direct-http-file";
    case PLAYER_MEDIA_PROFILE_GENERIC_HLS:
        return "generic-hls";
    case PLAYER_MEDIA_PROFILE_HEADER_SENSITIVE_HTTP:
        return "header-sensitive-http";
    case PLAYER_MEDIA_PROFILE_SIGNED_EPHEMERAL_URL:
        return "signed-ephemeral-url";
    case PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL:
        return "vendor-sensitive-url";
    case PLAYER_MEDIA_PROFILE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ingress_vendor_name(PlayerMediaVendor vendor)
{
    switch (vendor)
    {
    case PLAYER_MEDIA_VENDOR_BILIBILI:
        return "bilibili";
    case PLAYER_MEDIA_VENDOR_IQIYI:
        return "iqiyi";
    case PLAYER_MEDIA_VENDOR_MGTV:
        return "mgtv";
    case PLAYER_MEDIA_VENDOR_YOUKU:
        return "youku";
    case PLAYER_MEDIA_VENDOR_QQ_VIDEO:
        return "qq-video";
    case PLAYER_MEDIA_VENDOR_CCTV:
        return "cctv";
    case PLAYER_MEDIA_VENDOR_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ingress_format_name(PlayerMediaFormat format)
{
    switch (format)
    {
    case PLAYER_MEDIA_FORMAT_MP4:
        return "mp4";
    case PLAYER_MEDIA_FORMAT_FLV:
        return "flv";
    case PLAYER_MEDIA_FORMAT_HLS:
        return "hls";
    case PLAYER_MEDIA_FORMAT_DASH:
        return "dash";
    case PLAYER_MEDIA_FORMAT_MPEG_TS:
        return "mpeg-ts";
    case PLAYER_MEDIA_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

bool ingress_resolve(const char *uri, const char *metadata, PlayerMedia *out)
{
    bool likely_segmented = false;
    PlayerMediaVendor input_vendor;

    if (!uri || uri[0] == '\0' || !out)
        return false;

    ingress_reset(out);
    snprintf(out->uri, sizeof(out->uri), "%s", uri);
    snprintf(out->original_uri, sizeof(out->original_uri), "%s", uri);
    if (metadata)
        snprintf(out->metadata, sizeof(out->metadata), "%s", metadata);

    detect_metadata_mime(metadata, out->mime_type, sizeof(out->mime_type));
    out->format = detect_media_format(out->uri, out->mime_type[0] != '\0' ? out->mime_type : metadata, &likely_segmented);
    input_vendor = ingress_detect_vendor(uri, metadata);
    select_metadata_resource_if_better(uri, out, &likely_segmented, input_vendor);

    populate_media_flags(out, likely_segmented);
    snprintf(out->format_hint, sizeof(out->format_hint), "%s", ingress_format_name(out->format));

    if (ingress_vendor_is_sensitive(out->vendor))
        out->profile = PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL;
    else if (out->flags.is_hls)
        out->profile = PLAYER_MEDIA_PROFILE_GENERIC_HLS;
    else if (out->flags.is_signed)
        out->profile = PLAYER_MEDIA_PROFILE_SIGNED_EPHEMERAL_URL;
    else if (out->flags.is_http)
        out->profile = PLAYER_MEDIA_PROFILE_DIRECT_HTTP_FILE;

    policy_apply_hls(out);
    policy_apply_vendor(out);

    return true;
}
