#pragma once

/* Private syntax helpers shared by the parser and the AirPlay encoder. */
#include "request.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static inline bool rtsp_size_add(size_t left, size_t right, size_t *result_out)
{
    if (!result_out || right > SIZE_MAX - left)
        return false;
    *result_out = left + right;
    return true;
}

static inline bool rtsp_is_token_character(unsigned char value)
{
    if (isalnum(value))
        return true;
    switch (value)
    {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

static inline bool rtsp_valid_token(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    if (!cursor || *cursor == '\0')
        return false;
    while (*cursor)
    {
        if (!rtsp_is_token_character(*cursor))
            return false;
        cursor++;
    }
    return true;
}

static inline bool rtsp_valid_header_value(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    if (!cursor)
        return false;
    while (*cursor)
    {
        if (*cursor == '\r' || *cursor == '\n' || (*cursor < 0x20U && *cursor != '\t') || *cursor == 0x7fU)
            return false;
        cursor++;
    }
    return true;
}

static inline bool rtsp_valid_uri(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    size_t length = 0u;

    if (!cursor || *cursor == '\0')
        return false;
    while (*cursor)
    {
        if (*cursor < 0x21u || *cursor > 0x7eu)
            return false;
        cursor++;
        length++;
        if (length > RTSP_MAX_URI_BYTES)
            return false;
    }
    return true;
}

static inline bool rtsp_header_name_equal(const char *left, const char *right)
{
    return left && right && strcasecmp(left, right) == 0;
}
