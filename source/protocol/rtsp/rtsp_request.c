#include "syntax_internal.h"

static void rtsp_set_error(RtspError *error_out, RtspError error)
{
    if (error_out)
        *error_out = error;
}

static bool rtsp_copy_slice(char *destination,
                            size_t destination_size,
                            const uint8_t *start,
                            size_t length)
{
    if (!destination || destination_size == 0 || !start || length >= destination_size)
        return false;
    memcpy(destination, start, length);
    destination[length] = '\0';
    return true;
}

static bool rtsp_find_crlf(const uint8_t *bytes,
                           size_t start,
                           size_t limit,
                           size_t *line_end_out)
{
    size_t index;

    if (!bytes || !line_end_out || start > limit)
        return false;
    for (index = start; index + 1U < limit; ++index)
    {
        if (bytes[index] == '\r' && bytes[index + 1U] == '\n')
        {
            *line_end_out = index;
            return true;
        }
    }
    return false;
}

static bool rtsp_find_header_end(const uint8_t *bytes, size_t length, size_t *header_end_out)
{
    size_t index;

    if (!bytes || !header_end_out)
        return false;
    for (index = 0; index + 3U < length; ++index)
    {
        if (bytes[index] == '\r' && bytes[index + 1U] == '\n' &&
            bytes[index + 2U] == '\r' && bytes[index + 3U] == '\n')
        {
            *header_end_out = index + 4U;
            return true;
        }
    }
    return false;
}

static bool rtsp_parse_decimal(const char *text, uint64_t maximum, uint64_t *value_out)
{
    uint64_t value = 0;
    const unsigned char *cursor = (const unsigned char *)text;

    if (!cursor || !value_out || *cursor == '\0')
        return false;
    while (*cursor)
    {
        unsigned digit;
        if (!isdigit(*cursor))
            return false;
        digit = (unsigned)(*cursor - '0');
        if (value > (maximum - digit) / 10U)
            return false;
        value = value * 10U + digit;
        cursor++;
    }
    *value_out = value;
    return true;
}

static bool rtsp_decimal_syntax_valid(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (!cursor || *cursor == '\0')
        return false;
    while (*cursor)
    {
        if (!isdigit(*cursor))
            return false;
        cursor++;
    }
    return true;
}

const char *rtsp_request_header(const RtspRequest *request, const char *name)
{
    size_t index;

    if (!request || !name)
        return NULL;
    for (index = 0; index < request->header_count; ++index)
    {
        if (rtsp_header_name_equal(request->headers[index].name, name))
            return request->headers[index].value;
    }
    return NULL;
}

void rtsp_request_clear(RtspRequest *request)
{
    if (!request)
        return;
    free(request->body);
    memset(request, 0, sizeof(*request));
}

static bool rtsp_parse_request_line(const uint8_t *bytes,
                                    size_t line_length,
                                    RtspRequest *request)
{
    size_t first_space = SIZE_MAX;
    size_t second_space = SIZE_MAX;
    size_t index;

    if (!bytes || !request || line_length == 0)
        return false;
    for (index = 0; index < line_length; ++index)
    {
        if (bytes[index] == ' ')
        {
            if (first_space == SIZE_MAX)
                first_space = index;
            else if (second_space == SIZE_MAX)
                second_space = index;
            else
                return false;
        }
        else if (bytes[index] < 0x21U || bytes[index] > 0x7eU)
            return false;
    }
    if (first_space == SIZE_MAX || second_space == SIZE_MAX || first_space == 0 ||
        second_space <= first_space + 1U || second_space + 1U >= line_length)
    {
        return false;
    }
    if (!rtsp_copy_slice(request->method, sizeof(request->method), bytes, first_space) ||
        !rtsp_copy_slice(request->uri,
                         sizeof(request->uri),
                         bytes + first_space + 1U,
                         second_space - first_space - 1U) ||
        !rtsp_copy_slice(request->protocol,
                         sizeof(request->protocol),
                         bytes + second_space + 1U,
                         line_length - second_space - 1U) ||
        !rtsp_valid_token(request->method) ||
        (strcmp(request->protocol, "RTSP/1.0") != 0 && strcmp(request->protocol, "HTTP/1.1") != 0))
    {
        return false;
    }
    return true;
}

static bool rtsp_parse_header_line(const uint8_t *bytes,
                                   size_t line_length,
                                   RtspRequest *request)
{
    size_t colon = SIZE_MAX;
    size_t value_start;
    size_t value_end = line_length;
    size_t index;
    RtspHeader *header;

    if (!bytes || !request || line_length == 0 || request->header_count >= RTSP_MAX_HEADERS)
        return false;
    for (index = 0; index < line_length; ++index)
    {
        if (bytes[index] == ':' && colon == SIZE_MAX)
        {
            colon = index;
            continue;
        }
        if (bytes[index] == '\0' || bytes[index] == '\r' || bytes[index] == '\n' ||
            (bytes[index] < 0x20U && bytes[index] != '\t') || bytes[index] == 0x7fU)
        {
            return false;
        }
    }
    if (colon == SIZE_MAX || colon == 0)
        return false;
    value_start = colon + 1U;
    while (value_start < line_length && (bytes[value_start] == ' ' || bytes[value_start] == '\t'))
        value_start++;
    while (value_end > value_start && (bytes[value_end - 1U] == ' ' || bytes[value_end - 1U] == '\t'))
        value_end--;

    header = &request->headers[request->header_count];
    if (!rtsp_copy_slice(header->name, sizeof(header->name), bytes, colon) ||
        !rtsp_copy_slice(header->value,
                         sizeof(header->value),
                         bytes + value_start,
                         value_end - value_start) ||
        !rtsp_valid_token(header->name) || !rtsp_valid_header_value(header->value))
    {
        return false;
    }
    request->header_count++;
    return true;
}

RtspParseResult rtsp_parse_request(const uint8_t *bytes,
                                                  size_t length,
                                                  RtspRequest *request_out,
                                                  size_t *consumed_out,
                                                  RtspError *error_out)
{
    RtspRequest *request = NULL;
    size_t header_end;
    size_t line_end;
    size_t cursor;
    size_t total_length;
    const char *content_length_text;
    const char *cseq_text;
    uint64_t parsed_value;
    size_t index;

    if (!request_out || !consumed_out || (!bytes && length != 0))
    {
        rtsp_set_error(error_out, RTSP_ERROR_INVALID_ARGUMENT);
        return RTSP_PARSE_ERROR;
    }
    *consumed_out = 0;
    rtsp_set_error(error_out, RTSP_ERROR_NONE);
    if (length > RTSP_MAX_MESSAGE_BYTES)
    {
        rtsp_set_error(error_out, RTSP_ERROR_LIMIT_EXCEEDED);
        return RTSP_PARSE_ERROR;
    }
    if (!rtsp_find_header_end(bytes, length, &header_end))
    {
        if (length >= RTSP_MAX_HEADER_BYTES)
        {
            rtsp_set_error(error_out, RTSP_ERROR_LIMIT_EXCEEDED);
            return RTSP_PARSE_ERROR;
        }
        return RTSP_PARSE_NEED_MORE;
    }
    if (header_end > RTSP_MAX_HEADER_BYTES)
    {
        rtsp_set_error(error_out, RTSP_ERROR_LIMIT_EXCEEDED);
        return RTSP_PARSE_ERROR;
    }
    request = calloc(1, sizeof(*request));
    if (!request)
    {
        rtsp_set_error(error_out, RTSP_ERROR_OUT_OF_MEMORY);
        return RTSP_PARSE_ERROR;
    }
    if (!rtsp_find_crlf(bytes, 0, header_end, &line_end) ||
        !rtsp_parse_request_line(bytes, line_end, request))
        goto invalid_format;

    cursor = line_end + 2U;
    while (cursor + 2U < header_end)
    {
        if (!rtsp_find_crlf(bytes, cursor, header_end, &line_end) || line_end == cursor ||
            !rtsp_parse_header_line(bytes + cursor, line_end - cursor, request))
            goto invalid_format;
        cursor = line_end + 2U;
    }
    if (cursor != header_end - 2U)
        goto invalid_format;

    for (index = 0; index < request->header_count; ++index)
    {
        size_t later;
        if (rtsp_header_name_equal(request->headers[index].name, "Transfer-Encoding"))
            goto invalid_format;
        if (!rtsp_header_name_equal(request->headers[index].name, "Content-Length") &&
            !rtsp_header_name_equal(request->headers[index].name, "CSeq"))
        {
            continue;
        }
        for (later = index + 1U; later < request->header_count; ++later)
        {
            if (rtsp_header_name_equal(request->headers[index].name,
                                       request->headers[later].name))
                goto invalid_format;
        }
    }

    content_length_text = rtsp_request_header(request, "Content-Length");
    if (content_length_text)
    {
        if (!rtsp_decimal_syntax_valid(content_length_text))
            goto invalid_format;
        if (!rtsp_parse_decimal(content_length_text, RTSP_MAX_BODY_BYTES, &parsed_value))
            goto limit_exceeded;
        request->body_length = (size_t)parsed_value;
    }
    if (!rtsp_size_add(header_end, request->body_length, &total_length) ||
        total_length > RTSP_MAX_MESSAGE_BYTES)
        goto limit_exceeded;
    if (length < total_length)
    {
        free(request);
        return RTSP_PARSE_NEED_MORE;
    }

    cseq_text = rtsp_request_header(request, "CSeq");
    if (cseq_text)
    {
        if (!rtsp_decimal_syntax_valid(cseq_text) ||
            !rtsp_parse_decimal(cseq_text, UINT32_MAX, &parsed_value))
            goto invalid_format;
        request->has_cseq = true;
        request->cseq = (uint32_t)parsed_value;
    }
    if (request->body_length != 0)
    {
        request->body = malloc(request->body_length);
        if (!request->body)
            goto out_of_memory;
        memcpy(request->body, bytes + header_end, request->body_length);
    }

    *request_out = *request;
    free(request);
    *consumed_out = total_length;
    return RTSP_PARSE_OK;

invalid_format:
    rtsp_set_error(error_out, RTSP_ERROR_INVALID_FORMAT);
    goto failure;
limit_exceeded:
    rtsp_set_error(error_out, RTSP_ERROR_LIMIT_EXCEEDED);
    goto failure;
out_of_memory:
    rtsp_set_error(error_out, RTSP_ERROR_OUT_OF_MEMORY);
failure:
    free(request->body);
    free(request);
    return RTSP_PARSE_ERROR;
}
