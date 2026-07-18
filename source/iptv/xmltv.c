#include "iptv/xmltv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zlib.h>

#define XMLTV_PROGRAMME_MAX 16384

static void xmltv_copy(char *out, size_t out_size, const char *value)
{
    if (!out || out_size == 0)
        return;
    snprintf(out, out_size, "%s", value ? value : "");
}

static bool xmltv_attr(const char *tag, const char *name, char *out, size_t out_size)
{
    const char *p;
    size_t name_len;

    if (!tag || !name || !out || out_size == 0)
        return false;
    out[0] = '\0';
    name_len = strlen(name);
    for (p = tag; *p && *p != '>'; ++p)
    {
        if (strncasecmp(p, name, name_len) != 0)
            continue;
        if (p != tag && !isspace((unsigned char)p[-1]))
            continue;
        p += name_len;
        while (isspace((unsigned char)*p))
            ++p;
        if (*p != '=')
            continue;
        ++p;
        while (isspace((unsigned char)*p))
            ++p;
        if (*p != '\'' && *p != '"')
            continue;
        char quote = *p++;
        size_t used = 0;
        while (*p && *p != quote && used + 1 < out_size)
            out[used++] = *p++;
        out[used] = '\0';
        return used > 0;
    }
    return false;
}

static void xmltv_decode(const char *in, char *out, size_t out_size)
{
    size_t used = 0;

    if (!out || out_size == 0)
        return;
    while (in && *in && used + 1 < out_size)
    {
        if (*in == '&')
        {
            struct Entity { const char *encoded; char decoded; };
            static const struct Entity entities[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                {"&quot;", '"'}, {"&apos;", '\''}
            };
            bool matched = false;
            for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i)
            {
                size_t len = strlen(entities[i].encoded);
                if (strncmp(in, entities[i].encoded, len) == 0)
                {
                    out[used++] = entities[i].decoded;
                    in += len;
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;
        }
        out[used++] = *in++;
    }
    out[used] = '\0';
}

static bool xmltv_element_text(const char *xml, const char *element, char *out, size_t out_size)
{
    char opening[48];
    char closing[48];
    const char *start;
    const char *end;
    char encoded[IPTV_EPG_TITLE_MAX * 2];
    size_t len;

    snprintf(opening, sizeof(opening), "<%s", element);
    snprintf(closing, sizeof(closing), "</%s>", element);
    start = strstr(xml, opening);
    if (!start || !(start = strchr(start, '>')))
        return false;
    ++start;
    end = strstr(start, closing);
    if (!end)
        return false;
    len = (size_t)(end - start);
    if (len >= sizeof(encoded))
        len = sizeof(encoded) - 1;
    memcpy(encoded, start, len);
    encoded[len] = '\0';
    xmltv_decode(encoded, out, out_size);
    return out[0] != '\0';
}

static int64_t xmltv_days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static time_t xmltv_parse_time(const char *text)
{
    int year, month, day, hour, minute, second;
    int offset_sign = 1;
    int offset_hour = 0;
    int offset_minute = 0;
    const char *zone;
    int64_t seconds;

    if (!text || strlen(text) < 14 ||
        sscanf(text, "%4d%2d%2d%2d%2d%2d", &year, &month, &day, &hour, &minute, &second) != 6)
        return (time_t)0;
    zone = text + 14;
    while (*zone == ' ')
        ++zone;
    if (*zone == '+' || *zone == '-')
    {
        offset_sign = *zone == '-' ? -1 : 1;
        ++zone;
        if (strlen(zone) >= 4)
            sscanf(zone, "%2d%2d", &offset_hour, &offset_minute);
    }

    seconds = xmltv_days_from_civil(year, (unsigned)month, (unsigned)day) * 86400;
    seconds += hour * 3600 + minute * 60 + second;
    seconds -= offset_sign * (offset_hour * 3600 + offset_minute * 60);
    return (time_t)seconds;
}

static int xmltv_apply_programme(const char *xml,
                                 uint32_t source_id,
                                 IptvChannel *channels,
                                 int channel_count,
                                 time_t now)
{
    char encoded_channel_id[IPTV_TVG_ID_MAX * 2];
    char channel_id[IPTV_TVG_ID_MAX];
    char start_text[40];
    char stop_text[40];
    char title[IPTV_EPG_TITLE_MAX];
    time_t start;
    time_t stop;
    int touched = 0;

    if (!xmltv_attr(xml, "channel", encoded_channel_id, sizeof(encoded_channel_id)) ||
        !xmltv_attr(xml, "start", start_text, sizeof(start_text)) ||
        !xmltv_attr(xml, "stop", stop_text, sizeof(stop_text)) ||
        !xmltv_element_text(xml, "title", title, sizeof(title)))
        return 0;
    xmltv_decode(encoded_channel_id, channel_id, sizeof(channel_id));
    start = xmltv_parse_time(start_text);
    stop = xmltv_parse_time(stop_text);
    if (start <= 0 || stop <= start)
        return 0;

    for (int i = 0; i < channel_count; ++i)
    {
        IptvChannel *channel = &channels[i];
        if (channel->source_id != source_id || !channel->tvg_id[0] || strcmp(channel->tvg_id, channel_id) != 0)
            continue;
        if (start <= now && stop > now)
        {
            xmltv_copy(channel->now_title, sizeof(channel->now_title), title);
            channel->now_start = start;
            channel->now_stop = stop;
            ++touched;
        }
        else if (start > now && (channel->next_start == 0 || start < channel->next_start))
        {
            xmltv_copy(channel->next_title, sizeof(channel->next_title), title);
            channel->next_start = start;
            channel->next_stop = stop;
            ++touched;
        }
    }
    return touched;
}

int iptv_xmltv_apply_file(const char *path,
                          uint32_t source_id,
                          IptvChannel *channels,
                          int channel_count,
                          time_t now,
                          char *error,
                          size_t error_size)
{
    gzFile input;
    char line[2048];
    char *programme;
    size_t used = 0;
    bool collecting = false;
    int touched = 0;

    if (!path || !channels || channel_count <= 0)
        return 0;
    input = gzopen(path, "rb");
    if (!input)
    {
        if (error && error_size)
            snprintf(error, error_size, "cannot open XMLTV cache");
        return 0;
    }
    programme = malloc(XMLTV_PROGRAMME_MAX);
    if (!programme)
    {
        gzclose(input);
        if (error && error_size)
            snprintf(error, error_size, "out of memory parsing XMLTV");
        return 0;
    }

    while (gzgets(input, line, sizeof(line)))
    {
        char *start = line;
        if (!collecting)
        {
            start = strstr(line, "<programme");
            if (!start)
                continue;
            collecting = true;
            used = 0;
        }
        size_t length = strlen(start);
        if (used + length + 1 >= XMLTV_PROGRAMME_MAX)
        {
            collecting = false;
            used = 0;
            continue;
        }
        memcpy(programme + used, start, length);
        used += length;
        programme[used] = '\0';
        if (strstr(programme, "</programme>"))
        {
            touched += xmltv_apply_programme(programme, source_id, channels, channel_count, now);
            collecting = false;
            used = 0;
        }
    }

    free(programme);
    gzclose(input);
    if (error && error_size)
        error[0] = '\0';
    return touched;
}
