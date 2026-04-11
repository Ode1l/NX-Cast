#include "player/seek_target.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_spaces(const char *text)
{
    while (text && *text && isspace((unsigned char)*text))
        ++text;
    return text;
}

static const char *trim_end(const char *start)
{
    const char *end = start ? start + strlen(start) : NULL;

    while (end && end > start && isspace((unsigned char)end[-1]))
        --end;
    return end;
}

static bool parse_unsigned_ll(const char *start, const char *end, long long *out)
{
    long long value = 0;

    if (!start || !end || !out || start >= end)
        return false;

    for (const char *cursor = start; cursor < end; ++cursor)
    {
        int digit;

        if (!isdigit((unsigned char)*cursor))
            return false;
        digit = *cursor - '0';
        if (value > (LLONG_MAX - digit) / 10LL)
            return false;
        value = value * 10LL + digit;
    }

    *out = value;
    return true;
}

static bool parse_fraction_ms(const char *start, const char *end, int *out_ms)
{
    int millis = 0;
    int digits = 0;
    bool round_up = false;

    if (!out_ms)
        return false;
    if (start == end)
    {
        *out_ms = 0;
        return true;
    }

    for (const char *cursor = start; cursor < end; ++cursor)
    {
        if (!isdigit((unsigned char)*cursor))
            return false;
        if (digits < 3)
        {
            millis = millis * 10 + (*cursor - '0');
        }
        else if (digits == 3)
        {
            round_up = *cursor >= '5';
            break;
        }
        ++digits;
    }

    while (digits > 0 && digits < 3)
    {
        millis *= 10;
        ++digits;
    }

    if (round_up)
        ++millis;

    *out_ms = millis;
    return true;
}

static bool parse_hhmmss_ms(const char *start, const char *end, int *out_ms)
{
    const char *first_colon;
    const char *second_colon;
    const char *dot;
    const char *seconds_end;
    long long hour = 0;
    long long minute = 0;
    long long second = 0;
    long long total_ms = 0;
    int fraction_ms = 0;

    if (!start || !end || !out_ms || start >= end)
        return false;

    first_colon = memchr(start, ':', (size_t)(end - start));
    if (!first_colon)
        return false;
    second_colon = memchr(first_colon + 1, ':', (size_t)(end - (first_colon + 1)));
    if (!second_colon)
        return false;
    if (memchr(second_colon + 1, ':', (size_t)(end - (second_colon + 1))))
        return false;

    dot = memchr(second_colon + 1, '.', (size_t)(end - (second_colon + 1)));
    seconds_end = dot ? dot : end;

    if (!parse_unsigned_ll(start, first_colon, &hour) ||
        !parse_unsigned_ll(first_colon + 1, second_colon, &minute) ||
        !parse_unsigned_ll(second_colon + 1, seconds_end, &second))
        return false;

    if (dot && !parse_fraction_ms(dot + 1, end, &fraction_ms))
        return false;

    total_ms = ((hour * 3600LL) + (minute * 60LL) + second) * 1000LL + (long long)fraction_ms;
    if (fraction_ms >= 1000)
        total_ms += 1000 - fraction_ms;
    if (total_ms < 0 || total_ms > INT_MAX)
        return false;

    *out_ms = (int)total_ms;
    return true;
}

static bool parse_numeric_seconds_ms(const char *start, const char *end, int *out_ms)
{
    char *buffer;
    char *tail = NULL;
    double seconds;
    long long total_ms;

    if (!start || !end || !out_ms || start >= end)
        return false;

    buffer = malloc((size_t)(end - start) + 1);
    if (!buffer)
        return false;
    memcpy(buffer, start, (size_t)(end - start));
    buffer[end - start] = '\0';

    seconds = strtod(buffer, &tail);
    if (!tail || *tail != '\0' || seconds < 0.0)
    {
        free(buffer);
        return false;
    }

    total_ms = (long long)(seconds * 1000.0 + 0.5);
    free(buffer);
    if (total_ms < 0 || total_ms > INT_MAX)
        return false;

    *out_ms = (int)total_ms;
    return true;
}

bool player_seek_target_parse_ms(const char *target, int *out_ms)
{
    const char *start;
    const char *end;

    if (!target || !out_ms)
        return false;

    start = skip_spaces(target);
    if (!start || *start == '\0')
        return false;
    end = trim_end(start);
    if (!end || start >= end)
        return false;

    if (memchr(start, ':', (size_t)(end - start)))
        return parse_hhmmss_ms(start, end, out_ms);
    return parse_numeric_seconds_ms(start, end, out_ms);
}

char *player_seek_target_format_hhmmss_alloc(int position_ms)
{
    int total_seconds;
    int hour;
    int minute;
    int second;
    int needed;
    char *buffer;

    if (position_ms < 0)
        position_ms = 0;

    total_seconds = position_ms / 1000;
    hour = total_seconds / 3600;
    minute = (total_seconds % 3600) / 60;
    second = total_seconds % 60;

    needed = snprintf(NULL, 0, "%02d:%02d:%02d", hour, minute, second);
    if (needed < 0)
        return NULL;

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
        return NULL;

    snprintf(buffer, (size_t)needed + 1, "%02d:%02d:%02d", hour, minute, second);
    return buffer;
}
