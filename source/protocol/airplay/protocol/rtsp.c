#include "rtsp.h"
#include "protocol/rtsp/syntax_internal.h"

const char *airplay_rtsp_request_header(const AirPlayRtspRequest *request, const char *name)
{
    return rtsp_request_header(request, name);
}

void airplay_rtsp_request_clear(AirPlayRtspRequest *request)
{
    rtsp_request_clear(request);
}

AirPlayRtspParseResult airplay_rtsp_parse_request(const uint8_t *bytes,
                                                size_t length,
                                                AirPlayRtspRequest *request_out,
                                                size_t *consumed_out,
                                                AirPlayRtspError *error_out)
{
    return rtsp_parse_request(bytes, length, request_out, consumed_out, error_out);
}

static const char *rtsp_reason_phrase(int status_code)
{
    switch (status_code)
    {
    case 101:
        return "Switching Protocols";
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
    case 421:
        return "Misdirected Request";
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

    if (!response || body_length > AIRPLAY_RTSP_MAX_RESPONSE_BODY_BYTES ||
        (body_length != 0 && !body))
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
        response->body_length > AIRPLAY_RTSP_MAX_RESPONSE_BODY_BYTES ||
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
        total > AIRPLAY_RTSP_MAX_RESPONSE_MESSAGE_BYTES)
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

bool airplay_rtsp_outbound_request_encode(
    const AirPlayRtspOutboundRequest *request,
    uint8_t **bytes_out,
    size_t *length_out)
{
    char line[AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES +
              AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES + 8u];
    size_t total = 0u;
    size_t used = 0u;
    size_t index;
    int written;
    uint8_t *output;

    if (!request || !bytes_out || !length_out ||
        !rtsp_valid_token(request->method) || !rtsp_valid_uri(request->uri) ||
        !request->protocol || strcmp(request->protocol, "HTTP/1.1") != 0 ||
        request->header_count > AIRPLAY_RTSP_MAX_HEADERS ||
        (request->header_count != 0u && !request->headers) ||
        request->body_length > AIRPLAY_RTSP_MAX_BODY_BYTES ||
        (request->body_length != 0u && !request->body))
    {
        return false;
    }
    *bytes_out = NULL;
    *length_out = 0u;
    written = snprintf(line, sizeof(line), "%s %s %s\r\n",
                       request->method, request->uri, request->protocol);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !rtsp_size_add(total, (size_t)written, &total))
        return false;
    for (index = 0u; index < request->header_count; ++index)
    {
        const AirPlayRtspHeader *header = &request->headers[index];
        size_t later;

        if (!rtsp_valid_token(header->name) ||
            !rtsp_valid_header_value(header->value) ||
            strlen(header->name) > AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES ||
            strlen(header->value) > AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES ||
            rtsp_header_name_equal(header->name, "Content-Length"))
        {
            return false;
        }
        for (later = index + 1u; later < request->header_count; ++later)
        {
            if (rtsp_header_name_equal(header->name,
                                       request->headers[later].name))
                return false;
        }
        written = snprintf(line, sizeof(line), "%s: %s\r\n",
                           header->name, header->value);
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !rtsp_size_add(total, (size_t)written, &total))
            return false;
    }
    written = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                       request->body_length);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !rtsp_size_add(total, (size_t)written, &total) ||
        !rtsp_size_add(total, 2u, &total) ||
        !rtsp_size_add(total, request->body_length, &total) ||
        total > AIRPLAY_RTSP_MAX_MESSAGE_BYTES)
    {
        return false;
    }
    output = malloc(total + 1u);
    if (!output)
        return false;
    written = snprintf(line, sizeof(line), "%s %s %s\r\n",
                       request->method, request->uri, request->protocol);
    if (written < 0 ||
        !rtsp_encoded_append(output, total, &used, line, (size_t)written))
        goto failure;
    for (index = 0u; index < request->header_count; ++index)
    {
        written = snprintf(line, sizeof(line), "%s: %s\r\n",
                           request->headers[index].name,
                           request->headers[index].value);
        if (written < 0 ||
            !rtsp_encoded_append(output, total, &used, line, (size_t)written))
            goto failure;
    }
    written = snprintf(line, sizeof(line), "Content-Length: %zu\r\n",
                       request->body_length);
    if (written < 0 ||
        !rtsp_encoded_append(output, total, &used, line, (size_t)written) ||
        !rtsp_encoded_append(output, total, &used, "\r\n", 2u) ||
        !rtsp_encoded_append(output, total, &used, request->body,
                             request->body_length) ||
        used != total)
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
    session->logical_session_id = id;
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
    bool secondary_pair_verify;
    bool handled;

    if (!session || !request || !response_out || session->state == AIRPLAY_RTSP_SESSION_CLOSED ||
        !airplay_rtsp_response_init(response_out, request->protocol, 200))
    {
        return false;
    }
    discovery_request = strcmp(request->method, "GET") == 0 &&
                        (strstr(request->uri, "txtAirPlay") || strstr(request->uri, "txtRAOP"));
    secondary_pair_verify = strcmp(request->method, "POST") == 0 &&
                            strcmp(request->uri, "/pair-verify") == 0;
    if (strcmp(request->protocol, "RTSP/1.0") == 0 &&
        !request->has_cseq && !discovery_request && !secondary_pair_verify)
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
