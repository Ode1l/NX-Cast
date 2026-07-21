#include "rtsp.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void rtsp_set_error(AirPlayRtspError *error_out, AirPlayRtspError error)
{
    if (error_out)
        *error_out = error;
}

static bool rtsp_size_add(size_t left, size_t right, size_t *result_out)
{
    if (!result_out || right > SIZE_MAX - left)
        return false;
    *result_out = left + right;
    return true;
}

static bool rtsp_is_token_character(unsigned char value)
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

static bool rtsp_valid_token(const char *value)
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

static bool rtsp_valid_header_value(const char *value)
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

static bool rtsp_header_name_equal(const char *left, const char *right)
{
    return left && right && strcasecmp(left, right) == 0;
}

const char *airplay_rtsp_request_header(const AirPlayRtspRequest *request, const char *name)
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

void airplay_rtsp_request_clear(AirPlayRtspRequest *request)
{
    if (!request)
        return;
    free(request->body);
    memset(request, 0, sizeof(*request));
}

static bool rtsp_parse_request_line(const uint8_t *bytes,
                                    size_t line_length,
                                    AirPlayRtspRequest *request)
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
                                   AirPlayRtspRequest *request)
{
    size_t colon = SIZE_MAX;
    size_t value_start;
    size_t value_end = line_length;
    size_t index;
    AirPlayRtspHeader *header;

    if (!bytes || !request || line_length == 0 || request->header_count >= AIRPLAY_RTSP_MAX_HEADERS)
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

AirPlayRtspParseResult airplay_rtsp_parse_request(const uint8_t *bytes,
                                                  size_t length,
                                                  AirPlayRtspRequest *request_out,
                                                  size_t *consumed_out,
                                                  AirPlayRtspError *error_out)
{
    AirPlayRtspRequest *request = NULL;
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
        rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_INVALID_ARGUMENT);
        return AIRPLAY_RTSP_PARSE_ERROR;
    }
    *consumed_out = 0;
    rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_NONE);
    if (length > AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
    {
        rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);
        return AIRPLAY_RTSP_PARSE_ERROR;
    }
    if (!rtsp_find_header_end(bytes, length, &header_end))
    {
        if (length >= AIRPLAY_RTSP_MAX_HEADER_BYTES)
        {
            rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);
            return AIRPLAY_RTSP_PARSE_ERROR;
        }
        return AIRPLAY_RTSP_PARSE_NEED_MORE;
    }
    if (header_end > AIRPLAY_RTSP_MAX_HEADER_BYTES)
    {
        rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);
        return AIRPLAY_RTSP_PARSE_ERROR;
    }
    request = calloc(1, sizeof(*request));
    if (!request)
    {
        rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_OUT_OF_MEMORY);
        return AIRPLAY_RTSP_PARSE_ERROR;
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

    content_length_text = airplay_rtsp_request_header(request, "Content-Length");
    if (content_length_text)
    {
        if (!rtsp_decimal_syntax_valid(content_length_text))
            goto invalid_format;
        if (!rtsp_parse_decimal(content_length_text, AIRPLAY_RTSP_MAX_BODY_BYTES, &parsed_value))
            goto limit_exceeded;
        request->body_length = (size_t)parsed_value;
    }
    if (!rtsp_size_add(header_end, request->body_length, &total_length) ||
        total_length > AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
        goto limit_exceeded;
    if (length < total_length)
    {
        free(request);
        return AIRPLAY_RTSP_PARSE_NEED_MORE;
    }

    cseq_text = airplay_rtsp_request_header(request, "CSeq");
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
    return AIRPLAY_RTSP_PARSE_OK;

invalid_format:
    rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    goto failure;
limit_exceeded:
    rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);
    goto failure;
out_of_memory:
    rtsp_set_error(error_out, AIRPLAY_RTSP_ERROR_OUT_OF_MEMORY);
failure:
    free(request->body);
    free(request);
    return AIRPLAY_RTSP_PARSE_ERROR;
}

static const char *rtsp_reason_phrase(int status_code)
{
    switch (status_code)
    {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 405:
        return "Method Not Allowed";
    case 409:
        return "Conflict";
    case 404:
        return "Not Found";
    case 408:
        return "Request Timeout";
    case 413:
        return "Content Too Large";
    case 455:
        return "Method Not Valid in This State";
    case 461:
        return "Unsupported Transport";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

bool airplay_rtsp_response_init(AirPlayRtspResponse *response, const char *protocol, int status_code)
{
    if (!response || !protocol ||
        (strcmp(protocol, "RTSP/1.0") != 0 && strcmp(protocol, "HTTP/1.1") != 0))
    {
        return false;
    }
    memset(response, 0, sizeof(*response));
    memcpy(response->protocol, protocol, strlen(protocol) + 1U);
    return airplay_rtsp_response_set_status(response, status_code);
}

bool airplay_rtsp_response_set_status(AirPlayRtspResponse *response, int status_code)
{
    const char *reason;

    if (!response || status_code < 100 || status_code > 999)
        return false;
    reason = rtsp_reason_phrase(status_code);
    response->status_code = status_code;
    snprintf(response->reason, sizeof(response->reason), "%s", reason);
    return true;
}

bool airplay_rtsp_response_add_header(AirPlayRtspResponse *response,
                                      const char *name,
                                      const char *value)
{
    AirPlayRtspHeader *header;
    size_t name_length;
    size_t value_length;
    size_t index;

    if (!response || !name || !value || !rtsp_valid_token(name) || !rtsp_valid_header_value(value) ||
        rtsp_header_name_equal(name, "Content-Length") || response->header_count >= AIRPLAY_RTSP_MAX_HEADERS)
    {
        return false;
    }
    name_length = strlen(name);
    value_length = strlen(value);
    if (name_length > AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES ||
        value_length > AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES)
    {
        return false;
    }
    for (index = 0; index < response->header_count; ++index)
    {
        if (rtsp_header_name_equal(response->headers[index].name, name))
            return false;
    }
    header = &response->headers[response->header_count++];
    memcpy(header->name, name, name_length + 1U);
    memcpy(header->value, value, value_length + 1U);
    return true;
}

bool airplay_rtsp_response_set_body(AirPlayRtspResponse *response,
                                    const void *body,
                                    size_t body_length,
                                    const char *content_type)
{
    uint8_t *copy = NULL;

    if (!response || body_length > AIRPLAY_RTSP_MAX_BODY_BYTES || (body_length != 0 && !body))
        return false;
    if (body_length != 0)
    {
        copy = malloc(body_length);
        if (!copy)
            return false;
        memcpy(copy, body, body_length);
    }
    if (content_type && !airplay_rtsp_response_add_header(response, "Content-Type", content_type))
    {
        free(copy);
        return false;
    }
    free(response->body);
    response->body = copy;
    response->body_length = body_length;
    return true;
}

static bool rtsp_encoded_append(uint8_t *output,
                                size_t output_size,
                                size_t *used,
                                const void *bytes,
                                size_t length)
{
    if (!output || !used || (length != 0 && !bytes) || *used > output_size || length > output_size - *used)
        return false;
    if (length != 0)
        memcpy(output + *used, bytes, length);
    *used += length;
    return true;
}

bool airplay_rtsp_response_encode(const AirPlayRtspResponse *response,
                                  uint8_t **bytes_out,
                                  size_t *length_out)
{
    char line[AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES + AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES + 8U];
    size_t total = 0;
    size_t used = 0;
    size_t index;
    int written;
    uint8_t *output;

    if (!response || !bytes_out || !length_out || response->status_code < 100 ||
        response->body_length > AIRPLAY_RTSP_MAX_BODY_BYTES ||
        (response->body_length != 0 && !response->body))
    {
        return false;
    }
    *bytes_out = NULL;
    *length_out = 0;
    written = snprintf(line, sizeof(line), "%s %d %s\r\n",
                       response->protocol, response->status_code, response->reason);
    if (written < 0 || (size_t)written >= sizeof(line) || !rtsp_size_add(total, (size_t)written, &total))
        return false;
    for (index = 0; index < response->header_count; ++index)
    {
        written = snprintf(line, sizeof(line), "%s: %s\r\n",
                           response->headers[index].name, response->headers[index].value);
        if (written < 0 || (size_t)written >= sizeof(line) || !rtsp_size_add(total, (size_t)written, &total))
            return false;
    }
    written = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", response->body_length);
    if (written < 0 || (size_t)written >= sizeof(line) || !rtsp_size_add(total, (size_t)written, &total))
        return false;
    if (response->close_connection)
    {
        static const char connection_close[] = "Connection: close\r\n";
        if (!rtsp_size_add(total, sizeof(connection_close) - 1U, &total))
            return false;
    }
    if (!rtsp_size_add(total, 2U, &total) || !rtsp_size_add(total, response->body_length, &total) ||
        total > AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
    {
        return false;
    }
    output = malloc(total + 1U);
    if (!output)
        return false;

    written = snprintf(line, sizeof(line), "%s %d %s\r\n",
                       response->protocol, response->status_code, response->reason);
    if (written < 0 || !rtsp_encoded_append(output, total, &used, line, (size_t)written))
        goto failure;
    for (index = 0; index < response->header_count; ++index)
    {
        written = snprintf(line, sizeof(line), "%s: %s\r\n",
                           response->headers[index].name, response->headers[index].value);
        if (written < 0 || !rtsp_encoded_append(output, total, &used, line, (size_t)written))
            goto failure;
    }
    written = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", response->body_length);
    if (written < 0 || !rtsp_encoded_append(output, total, &used, line, (size_t)written))
        goto failure;
    if (response->close_connection)
    {
        static const char connection_close[] = "Connection: close\r\n";
        if (!rtsp_encoded_append(output, total, &used, connection_close, sizeof(connection_close) - 1U))
            goto failure;
    }
    if (!rtsp_encoded_append(output, total, &used, "\r\n", 2U) ||
        !rtsp_encoded_append(output, total, &used, response->body, response->body_length) || used != total)
    {
        goto failure;
    }
    output[total] = '\0';
    *bytes_out = output;
    *length_out = total;
    return true;

failure:
    free(output);
    return false;
}

void airplay_rtsp_response_clear(AirPlayRtspResponse *response)
{
    if (!response)
        return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}

void airplay_rtsp_session_init(AirPlayRtspSession *session, uint64_t id)
{
    if (!session)
        return;
    memset(session, 0, sizeof(*session));
    session->id = id;
    session->state = AIRPLAY_RTSP_SESSION_CONNECTED;
}

void airplay_rtsp_session_set_peer_ipv4(AirPlayRtspSession *session,
                                        uint32_t address)
{
    if (session)
        session->peer_ipv4_address = address;
}

bool airplay_rtsp_default_route(AirPlayRtspSession *session,
                                const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response)
{
    if (strcmp(request->method, "OPTIONS") == 0)
    {
        return airplay_rtsp_response_add_header(response,
                                                "Public",
                                                "OPTIONS, GET, POST, SETUP, RECORD, GET_PARAMETER, SET_PARAMETER, TEARDOWN");
    }
    if (strcmp(request->method, "SETUP") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_CONNECTED)
            return airplay_rtsp_response_set_status(response, 455);
        session->state = AIRPLAY_RTSP_SESSION_SETUP;
        return true;
    }
    if (strcmp(request->method, "RECORD") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP)
            return airplay_rtsp_response_set_status(response, 455);
        session->state = AIRPLAY_RTSP_SESSION_RECORDING;
        return true;
    }
    if (strcmp(request->method, "GET_PARAMETER") == 0 ||
        strcmp(request->method, "SET_PARAMETER") == 0)
    {
        if (session->state != AIRPLAY_RTSP_SESSION_SETUP &&
            session->state != AIRPLAY_RTSP_SESSION_RECORDING)
        {
            return airplay_rtsp_response_set_status(response, 455);
        }
        return true;
    }
    if (strcmp(request->method, "TEARDOWN") == 0)
    {
        if (session->state == AIRPLAY_RTSP_SESSION_CLOSED)
            return airplay_rtsp_response_set_status(response, 455);
        session->state = AIRPLAY_RTSP_SESSION_CLOSED;
        response->close_connection = true;
        return true;
    }
    return airplay_rtsp_response_set_status(response,
                                             strcmp(request->method, "GET") == 0 ||
                                                     strcmp(request->method, "POST") == 0
                                                 ? 404
                                                 : 501);
}

static bool rtsp_add_control_response_headers(
    AirPlayRtspResponse *response,
    const AirPlayRtspRequest *request)
{
    char cseq[16];

    if (!airplay_rtsp_response_add_header(response, "Server",
                                          "AirTunes/220.68"))
        return false;
    if (!request->has_cseq)
        return true;
    snprintf(cseq, sizeof(cseq), "%u", request->cseq);
    if (!airplay_rtsp_response_add_header(response, "CSeq", cseq))
        return false;
    if (strcmp(request->protocol, "RTSP/1.0") == 0 &&
        strcmp(request->method, "RECORD") != 0 &&
        !airplay_rtsp_response_add_header(
            response, "Audio-Jack-Status", "connected; type=digital"))
        return false;
    return true;
}

bool airplay_rtsp_dispatch(AirPlayRtspSession *session,
                           const AirPlayRtspRequest *request,
                           AirPlayRtspRouteHandler handler,
                           void *user_data,
                           AirPlayRtspResponse *response_out)
{
    bool discovery_request;
    bool handled;

    if (!session || !request || !response_out || session->state == AIRPLAY_RTSP_SESSION_CLOSED ||
        !airplay_rtsp_response_init(response_out, request->protocol, 200))
    {
        return false;
    }
    discovery_request = strcmp(request->method, "GET") == 0 &&
                        (strstr(request->uri, "txtAirPlay") || strstr(request->uri, "txtRAOP"));
    if (strcmp(request->protocol, "RTSP/1.0") == 0 && !request->has_cseq && !discovery_request)
    {
        response_out->close_connection = true;
        return airplay_rtsp_response_set_status(response_out, 400);
    }
    if (!rtsp_add_control_response_headers(response_out, request))
        return false;
    session->request_count++;
    handled = handler ? handler(session, request, response_out, user_data)
                      : airplay_rtsp_default_route(session, request, response_out);
    if (!handled)
    {
        airplay_rtsp_response_clear(response_out);
        if (!airplay_rtsp_response_init(response_out, request->protocol, 500))
            return false;
        if (!rtsp_add_control_response_headers(response_out, request))
            return false;
    }
    return true;
}

const char *airplay_rtsp_error_name(AirPlayRtspError error)
{
    switch (error)
    {
    case AIRPLAY_RTSP_ERROR_NONE:
        return "none";
    case AIRPLAY_RTSP_ERROR_INVALID_ARGUMENT:
        return "invalid-argument";
    case AIRPLAY_RTSP_ERROR_INVALID_FORMAT:
        return "invalid-format";
    case AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED:
        return "limit-exceeded";
    case AIRPLAY_RTSP_ERROR_OUT_OF_MEMORY:
        return "out-of-memory";
    default:
        return "unknown";
    }
}

const char *airplay_rtsp_session_state_name(AirPlayRtspSessionState state)
{
    switch (state)
    {
    case AIRPLAY_RTSP_SESSION_CONNECTED:
        return "connected";
    case AIRPLAY_RTSP_SESSION_SETUP:
        return "setup";
    case AIRPLAY_RTSP_SESSION_RECORDING:
        return "recording";
    case AIRPLAY_RTSP_SESSION_CLOSED:
        return "closed";
    default:
        return "unknown";
    }
}
