#include "player/ingress/resource_select.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "log/log.h"
#include "player/ingress/classify.h"
#include "player/ingress/vendor.h"

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

static void clip_for_log(const char *input, char *output, size_t output_size)
{
    size_t length;

    if (!output || output_size == 0)
        return;

    output[0] = '\0';
    if (!input || input[0] == '\0')
        return;

    length = strlen(input);
    if (length + 1 <= output_size)
    {
        memcpy(output, input, length + 1);
        return;
    }

    if (output_size <= 4)
    {
        size_t copy_len = output_size - 1;
        memcpy(output, input, copy_len);
        output[copy_len] = '\0';
        return;
    }

    memcpy(output, input, output_size - 4);
    memcpy(output + output_size - 4, "...", 4);
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

static int score_metadata_resource(const char *current_uri, const PlayerMetadataResource *resource,
                                   PlayerMediaVendor vendor)
{
    int score = 0;

    if (!resource || resource->uri[0] == '\0')
        return -100000;

    if (contains_ignore_case(resource->protocol_info, "http-get:"))
        score += 120;
    else if (resource->protocol_info[0] != '\0')
        score -= 120;

    if (resource->mime_type[0] != '\0')
        score += 25;

    if (contains_ignore_case(resource->protocol_info, "dlna.org_op=11") ||
        contains_ignore_case(resource->protocol_info, "dlna.org_op=01"))
        score += 40;
    else if (contains_ignore_case(resource->protocol_info, "dlna.org_op=00"))
        score -= 20;

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
                resource->format = ingress_classify_format(resource->uri,
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

static void log_metadata_candidates(const PlayerMetadataResource *resources, int count,
                                    PlayerMediaVendor vendor, int baseline_score,
                                    int best_index, int best_score)
{
    if (!ingress_vendor_is_sensitive(vendor) || !resources || count <= 0)
        return;

    log_info("[player-ingress] metadata_candidates vendor=%s count=%d baseline_score=%d best_index=%d best_score=%d\n",
             ingress_vendor_name(vendor),
             count,
             baseline_score,
             best_index,
             best_score);

    for (int i = 0; i < count; ++i)
    {
        char clipped_uri[256];
        char clipped_protocol[192];
        char clipped_mime[96];

        clip_for_log(resources[i].uri, clipped_uri, sizeof(clipped_uri));
        clip_for_log(resources[i].protocol_info, clipped_protocol, sizeof(clipped_protocol));
        clip_for_log(resources[i].mime_type, clipped_mime, sizeof(clipped_mime));

        log_info("[player-ingress] metadata_res vendor=%s index=%d score=%d exact=%d format=%s segmented=%d mime=%s protocol_info=%s uri=%s\n",
                 ingress_vendor_name(vendor),
                 i,
                 resources[i].score,
                 resources[i].exact_uri_match ? 1 : 0,
                 ingress_format_name(resources[i].format),
                 resources[i].likely_segmented ? 1 : 0,
                 clipped_mime[0] != '\0' ? clipped_mime : "<none>",
                 clipped_protocol[0] != '\0' ? clipped_protocol : "<none>",
                 clipped_uri[0] != '\0' ? clipped_uri : "<empty>");
    }
}

void ingress_select_metadata_resource(const char *input_uri,
                                      PlayerMedia *media,
                                      bool *likely_segmented,
                                      PlayerMediaVendor vendor)
{
    PlayerMetadataResource resources[PLAYER_MEDIA_METADATA_RESOURCE_MAX];
    PlayerMetadataResource baseline;
    int resource_count;
    int best_index = -1;
    int best_score = -100000;
    int baseline_score;

    if (!media || !likely_segmented)
        return;

    resource_count = parse_metadata_resources(media->metadata,
                                              input_uri,
                                              resources,
                                              PLAYER_MEDIA_METADATA_RESOURCE_MAX,
                                              vendor);
    media->metadata_candidate_count = resource_count;
    if (resource_count <= 0)
        return;

    memset(&baseline, 0, sizeof(baseline));
    snprintf(baseline.uri, sizeof(baseline.uri), "%s", input_uri ? input_uri : "");
    snprintf(baseline.mime_type, sizeof(baseline.mime_type), "%s", media->mime_type);
    baseline.format = media->format;
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

    log_metadata_candidates(resources, resource_count, vendor, baseline_score, best_index, best_score);

    if (best_score < baseline_score && !resources[best_index].exact_uri_match)
    {
        log_info("[player-ingress] metadata_select vendor=%s selected=0 reason=baseline-better baseline_score=%d best_score=%d best_index=%d\n",
                 ingress_vendor_name(vendor),
                 baseline_score,
                 best_score,
                 best_index);
        return;
    }

    snprintf(media->uri, sizeof(media->uri), "%s", resources[best_index].uri);
    snprintf(media->protocol_info, sizeof(media->protocol_info), "%s", resources[best_index].protocol_info);
    if (resources[best_index].mime_type[0] != '\0')
        snprintf(media->mime_type, sizeof(media->mime_type), "%s", resources[best_index].mime_type);
    media->format = resources[best_index].format;
    media->selected_from_metadata = true;
    *likely_segmented = resources[best_index].likely_segmented;

    {
        char clipped_uri[256];
        clip_for_log(media->uri, clipped_uri, sizeof(clipped_uri));
        log_info("[player-ingress] metadata_select vendor=%s selected=1 best_index=%d best_score=%d selected_uri=%s\n",
                 ingress_vendor_name(vendor),
                 best_index,
                 best_score,
                 clipped_uri[0] != '\0' ? clipped_uri : "<empty>");
    }
}
