#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol/airplay/protocol/rtsp.h"

static AirPlayRtspRequest parse_complete(const char *message, size_t *consumed_out)
{
    AirPlayRtspRequest request = {0};
    AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
    size_t consumed = 0;

    assert(message != NULL);
    assert(airplay_rtsp_parse_request((const uint8_t *)message,
                                      strlen(message),
                                      &request,
                                      &consumed,
                                      &error) == AIRPLAY_RTSP_PARSE_OK);
    assert(error == AIRPLAY_RTSP_ERROR_NONE);
    if (consumed_out)
        *consumed_out = consumed;
    return request;
}

static void expect_parse_error(const char *message, AirPlayRtspError expected)
{
    AirPlayRtspRequest request = {0};
    AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
    size_t consumed = 0;

    assert(airplay_rtsp_parse_request((const uint8_t *)message,
                                      strlen(message),
                                      &request,
                                      &consumed,
                                      &error) == AIRPLAY_RTSP_PARSE_ERROR);
    assert(error == expected);
    assert(consumed == 0);
    airplay_rtsp_request_clear(&request);
}

static void test_fragmented_body_request(void)
{
    static const char body[] = "volume: -12.0\r\n";
    char message[512];
    size_t message_length;
    size_t length;

    int written = snprintf(message,
                           sizeof(message),
                           "SET_PARAMETER rtsp://receiver/stream RTSP/1.0\r\n"
                           "cseq: 42\r\n"
                           "Content-Type: text/parameters\r\n"
                           "Content-Length: %zu\r\n"
                           "\r\n"
                           "%s",
                           sizeof(body) - 1U,
                           body);
    assert(written > 0 && (size_t)written < sizeof(message));
    message_length = (size_t)written;
    for (length = 0; length < message_length; ++length)
    {
        AirPlayRtspRequest request = {0};
        AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
        size_t consumed = 0;
        assert(airplay_rtsp_parse_request((const uint8_t *)message,
                                          length,
                                          &request,
                                          &consumed,
                                          &error) == AIRPLAY_RTSP_PARSE_NEED_MORE);
        assert(error == AIRPLAY_RTSP_ERROR_NONE);
    }

    AirPlayRtspRequest request = {0};
    AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
    size_t consumed = 0;
    assert(airplay_rtsp_parse_request((const uint8_t *)message,
                                      message_length,
                                      &request,
                                      &consumed,
                                      &error) == AIRPLAY_RTSP_PARSE_OK);
    assert(consumed == message_length);
    assert(strcmp(request.method, "SET_PARAMETER") == 0);
    assert(strcmp(request.uri, "rtsp://receiver/stream") == 0);
    assert(request.has_cseq && request.cseq == 42U);
    assert(strcmp(airplay_rtsp_request_header(&request, "CONTENT-TYPE"), "text/parameters") == 0);
    assert(request.body_length == sizeof(body) - 1U);
    assert(memcmp(request.body, body, sizeof(body) - 1U) == 0);
    airplay_rtsp_request_clear(&request);
}

static void test_pipelined_requests(void)
{
    static const char first[] =
        "OPTIONS * RTSP/1.0\r\n"
        "CSeq: 1\r\n"
        "\r\n";
    static const char second[] =
        "SETUP rtsp://receiver/stream RTSP/1.0\r\n"
        "CSeq: 2\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    char pipeline[sizeof(first) + sizeof(second)];
    size_t first_consumed;
    AirPlayRtspRequest request;

    memcpy(pipeline, first, sizeof(first) - 1U);
    memcpy(pipeline + sizeof(first) - 1U, second, sizeof(second));
    request = parse_complete(pipeline, &first_consumed);
    assert(first_consumed == sizeof(first) - 1U);
    assert(strcmp(request.method, "OPTIONS") == 0);
    airplay_rtsp_request_clear(&request);

    request = parse_complete(pipeline + first_consumed, NULL);
    assert(strcmp(request.method, "SETUP") == 0);
    assert(request.cseq == 2U);
    airplay_rtsp_request_clear(&request);
}

static void test_parse_failures(void)
{
    char *large_header = malloc(AIRPLAY_RTSP_MAX_HEADER_BYTES);

    assert(large_header != NULL);
    expect_parse_error("OPTIONS * RTSP/1.0\r\nCSeq: nope\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    expect_parse_error("OPTIONS * RTSP/1.0\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    expect_parse_error("OPTIONS * RTSP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    expect_parse_error("POST /pair-setup RTSP/1.0\r\nCSeq: 1\r\nContent-Length: nope\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    expect_parse_error("OPTIONS  * RTSP/1.0\r\nCSeq: 1\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_INVALID_FORMAT);
    expect_parse_error("POST /pair-setup RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 1048577\r\n\r\n",
                       AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);

    memset(large_header, 'A', AIRPLAY_RTSP_MAX_HEADER_BYTES);
    {
        AirPlayRtspRequest request = {0};
        AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
        size_t consumed = 0;
        assert(airplay_rtsp_parse_request((const uint8_t *)large_header,
                                          AIRPLAY_RTSP_MAX_HEADER_BYTES,
                                          &request,
                                          &consumed,
                                          &error) == AIRPLAY_RTSP_PARSE_ERROR);
        assert(error == AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED);
    }
    free(large_header);
}

static void test_response_and_session(void)
{
    static const char options[] = "OPTIONS * RTSP/1.0\r\nCSeq: 7\r\n\r\n";
    static const char setup[] = "SETUP rtsp://receiver/stream RTSP/1.0\r\nCSeq: 8\r\n\r\n";
    static const char record[] = "RECORD rtsp://receiver/stream RTSP/1.0\r\nCSeq: 9\r\n\r\n";
    static const char get_parameter[] = "GET_PARAMETER rtsp://receiver/stream RTSP/1.0\r\nCSeq: 10\r\n\r\n";
    static const char teardown[] = "TEARDOWN rtsp://receiver/stream RTSP/1.0\r\nCSeq: 11\r\n\r\n";
    static const char missing_cseq[] = "OPTIONS * RTSP/1.0\r\n\r\n";
    static const char discovery[] = "GET /info?txtAirPlay RTSP/1.0\r\n\r\n";
    const char *messages[] = {options, setup, record, get_parameter, teardown};
    const AirPlayRtspSessionState states[] = {
        AIRPLAY_RTSP_SESSION_CONNECTED,
        AIRPLAY_RTSP_SESSION_SETUP,
        AIRPLAY_RTSP_SESSION_RECORDING,
        AIRPLAY_RTSP_SESSION_RECORDING,
        AIRPLAY_RTSP_SESSION_CLOSED
    };
    AirPlayRtspSession session;
    size_t index;

    airplay_rtsp_session_init(&session, 99U);
    for (index = 0; index < sizeof(messages) / sizeof(messages[0]); ++index)
    {
        AirPlayRtspRequest request = parse_complete(messages[index], NULL);
        AirPlayRtspResponse response = {0};
        uint8_t *encoded = NULL;
        size_t encoded_length = 0;
        assert(airplay_rtsp_dispatch(&session, &request, NULL, NULL, &response));
        assert(response.status_code == 200);
        assert(session.state == states[index]);
        assert(airplay_rtsp_response_encode(&response, &encoded, &encoded_length));
        assert(encoded_length > 0);
        assert(strstr((const char *)encoded, "Content-Length: 0\r\n") != NULL);
        assert(strstr((const char *)encoded, "Server: AirTunes/220.68\r\n") != NULL);
        assert(strstr((const char *)encoded, "CSeq:") != NULL);
        if (strcmp(request.method, "RECORD") == 0)
            assert(strstr((const char *)encoded, "Audio-Jack-Status:") == NULL);
        else
            assert(strstr((const char *)encoded,
                          "Audio-Jack-Status: connected; type=digital\r\n") != NULL);
        if (index == 0)
            assert(strstr((const char *)encoded, "Public:") != NULL);
        free(encoded);
        airplay_rtsp_response_clear(&response);
        airplay_rtsp_request_clear(&request);
    }
    assert(session.request_count == 5U);

    airplay_rtsp_session_init(&session, 100U);
    {
        AirPlayRtspRequest request = parse_complete(record, NULL);
        AirPlayRtspResponse response = {0};
        assert(airplay_rtsp_dispatch(&session, &request, NULL, NULL, &response));
        assert(response.status_code == 455);
        assert(session.state == AIRPLAY_RTSP_SESSION_CONNECTED);
        airplay_rtsp_response_clear(&response);
        airplay_rtsp_request_clear(&request);
    }
    {
        AirPlayRtspRequest request = parse_complete(missing_cseq, NULL);
        AirPlayRtspResponse response = {0};
        assert(airplay_rtsp_dispatch(&session, &request, NULL, NULL, &response));
        assert(response.status_code == 400 && response.close_connection);
        airplay_rtsp_response_clear(&response);
        airplay_rtsp_request_clear(&request);
    }

    airplay_rtsp_session_init(&session, 101U);
    {
        AirPlayRtspRequest request = parse_complete(discovery, NULL);
        AirPlayRtspResponse response = {0};
        assert(airplay_rtsp_dispatch(&session, &request, NULL, NULL, &response));
        assert(response.status_code == 404 && !response.close_connection);
        airplay_rtsp_response_clear(&response);
        airplay_rtsp_request_clear(&request);
    }
}

static void test_response_body(void)
{
    static const uint8_t body[] = {0x62, 0x70, 0x6c, 0x69, 0x73, 0x74};
    AirPlayRtspResponse response;
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;

    assert(airplay_rtsp_response_init(&response, "HTTP/1.1", 200));
    assert(airplay_rtsp_response_add_header(&response, "X-Apple-Session-ID", "test-session"));
    assert(!airplay_rtsp_response_add_header(&response, "Content-Length", "6"));
    assert(!airplay_rtsp_response_add_header(&response, "Bad\r\nHeader", "value"));
    assert(airplay_rtsp_response_set_body(&response,
                                          body,
                                          sizeof(body),
                                          "application/x-apple-binary-plist"));
    assert(airplay_rtsp_response_encode(&response, &encoded, &encoded_length));
    assert(strstr((const char *)encoded, "Content-Length: 6\r\n") != NULL);
    assert(memcmp(encoded + encoded_length - sizeof(body), body, sizeof(body)) == 0);
    free(encoded);
    airplay_rtsp_response_clear(&response);
}

int main(void)
{
    AirPlayRtspRequest request = {0};
    AirPlayRtspError error = AIRPLAY_RTSP_ERROR_NONE;
    size_t consumed = 0;

    assert(airplay_rtsp_parse_request(NULL, 1, &request, &consumed, &error) == AIRPLAY_RTSP_PARSE_ERROR);
    assert(error == AIRPLAY_RTSP_ERROR_INVALID_ARGUMENT);
    assert(strcmp(airplay_rtsp_error_name(AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED), "limit-exceeded") == 0);
    assert(strcmp(airplay_rtsp_session_state_name(AIRPLAY_RTSP_SESSION_RECORDING), "recording") == 0);

    test_fragmented_body_request();
    test_pipelined_requests();
    test_parse_failures();
    test_response_and_session();
    test_response_body();

    puts("AirPlay RTSP tests passed");
    return 0;
}
