#include "response.h"

#include <string.h>
#include <strings.h>

static bool token(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           (c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

static bool text_byte(uint8_t c)
{
    return c == '\t' || (c >= 0x20 && c != 0x7f);
}

static bool decimal(const uint8_t *p, size_t n, uint64_t max, uint64_t *out)
{
    uint64_t value = 0;
    if (!n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return false;
        unsigned digit = p[i] - '0';
        if (value > max / 10 || (value == max / 10 && digit > max % 10)) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

static bool name_is(const uint8_t *p, size_t n, const char *name)
{
    return strlen(name) == n && !strncasecmp((const char *)p, name, n);
}

RtspParseResult wfd_response_parse(const uint8_t *bytes, size_t length,
                                   WfdResponse *out, size_t *consumed,
                                   RtspError *error)
{
    if (consumed) *consumed = 0;
    if (error) *error = RTSP_ERROR_NONE;
    if (!out || !consumed || (!bytes && length)) {
        if (error) *error = RTSP_ERROR_INVALID_ARGUMENT;
        return RTSP_PARSE_ERROR;
    }
    WfdResponse parsed = {0};
    size_t cursor = 0, lines = 0;
    bool got_cseq = false, got_length = false, got_public = false;
    size_t limit = length < RTSP_MAX_HEADER_BYTES ? length : RTSP_MAX_HEADER_BYTES;
    while (true) {
        size_t end = cursor;
        while (end < limit && bytes[end] != '\r') {
            if (!text_byte(bytes[end])) goto invalid;
            ++end;
        }
        if (end == limit || end + 1 == limit) goto incomplete_header;
        if (bytes[end + 1] != '\n') goto invalid;
        size_t n = end - cursor;
        const uint8_t *line = bytes + cursor;
        cursor = end + 2;
        if (lines == 0) {
            uint64_t code;
            if (n < 13 || memcmp(line, "RTSP/1.0 ", 9) || line[12] != ' ' ||
                !decimal(line + 9, 3, 599, &code) || code < 100) goto invalid;
            parsed.status = (unsigned)code;
            ++lines;
            continue;
        }
        if (!n) break;
        if (++lines > RTSP_MAX_HEADERS + 1) goto too_large;
        size_t colon = 0;
        while (colon < n && line[colon] != ':') {
            if (!token(line[colon])) goto invalid;
            ++colon;
        }
        if (!colon || colon == n) goto invalid;
        if (colon > RTSP_MAX_HEADER_NAME_BYTES) goto too_large;
        size_t first = colon + 1, last = n;
        while (first < last && (line[first] == ' ' || line[first] == '\t')) ++first;
        while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t')) --last;
        size_t value_length = last - first;
        if (value_length > RTSP_MAX_HEADER_VALUE_BYTES) goto too_large;
        uint64_t number;
        if (name_is(line, colon, "CSeq")) {
            if (got_cseq || !decimal(line + first, value_length, UINT32_MAX, &number)) goto invalid;
            parsed.cseq = (uint32_t)number;
            got_cseq = true;
        } else if (name_is(line, colon, "Content-Length")) {
            if (got_length) goto invalid;
            if (!value_length) goto invalid;
            for (size_t i = first; i < last; ++i)
                if (line[i] < '0' || line[i] > '9') goto invalid;
            if (!decimal(line + first, value_length, RTSP_MAX_BODY_BYTES, &number)) goto too_large;
            parsed.body_length = (size_t)number;
            got_length = true;
        } else if (name_is(line, colon, "Public")) {
            size_t offset = strlen(parsed.public_methods);
            size_t separator = got_public ? 2 : 0;
            if (offset + separator + value_length > RTSP_MAX_HEADER_VALUE_BYTES) goto too_large;
            if (separator) {
                memcpy(parsed.public_methods + offset, ", ", separator);
                offset += separator;
            }
            memcpy(parsed.public_methods + offset, line + first, value_length);
            parsed.public_methods[offset + value_length] = 0;
            got_public = true;
        } else if (name_is(line, colon, "Transfer-Encoding")) {
            goto invalid;
        }
    }
    if (!got_cseq || (parsed.status < 200 && parsed.body_length)) goto invalid;
    if (parsed.body_length > length - cursor) return RTSP_PARSE_NEED_MORE;
    parsed.body = parsed.body_length ? bytes + cursor : NULL;
    *out = parsed;
    *consumed = cursor + parsed.body_length;
    return RTSP_PARSE_OK;

incomplete_header:
    if (length < RTSP_MAX_HEADER_BYTES) return RTSP_PARSE_NEED_MORE;
too_large:
    if (error) *error = RTSP_ERROR_LIMIT_EXCEEDED;
    return RTSP_PARSE_ERROR;
invalid:
    if (error) *error = RTSP_ERROR_INVALID_FORMAT;
    return RTSP_PARSE_ERROR;
}
