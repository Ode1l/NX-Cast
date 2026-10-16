#include "options.h"
#include "response.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char reply[] =
    "RTSP/1.0 200 OK\r\ncseq: 7\r\n"
    "Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, SETUP, PLAY, TEARDOWN\r\n"
    "Content-Length: 0\r\n\r\n";

static void response_prefixes(void)
{
    WfdResponse sentinel = {.status = 451, .cseq = 987};
    for (size_t i = 0; i < strlen(reply); ++i) {
        WfdResponse out = sentinel;
        size_t used = 123;
        RtspError error = RTSP_ERROR_INVALID_FORMAT;
        assert(wfd_response_parse((const uint8_t *)reply, i, &out, &used, &error) == RTSP_PARSE_NEED_MORE);
        assert(!used && error == RTSP_ERROR_NONE);
        assert(out.status == sentinel.status && out.cseq == sentinel.cseq);
    }
    WfdResponse out;
    size_t used;
    assert(wfd_response_parse((const uint8_t *)reply, strlen(reply), &out, &used, NULL) == RTSP_PARSE_OK);
    assert(out.status == 200 && out.cseq == 7 && used == strlen(reply));
    assert(out.body == NULL && out.body_length == 0);

    static const char binary[] = "RTSP/1.0 400 Error\r\nCSeq: 0\r\nContent-Length: 4\r\n\r\n\0\r\nXNEXT";
    size_t header = (size_t)(strstr(binary, "\r\n\r\n") - binary) + 4;
    for (size_t n = header; n < header + 4; ++n)
        assert(wfd_response_parse((const uint8_t *)binary, n, &out, &used, NULL) == RTSP_PARSE_NEED_MORE);
    assert(wfd_response_parse((const uint8_t *)binary, sizeof(binary) - 1, &out, &used, NULL) == RTSP_PARSE_OK);
    assert(used == header + 4 && out.body_length == 4);
    assert(!memcmp(out.body, "\0\r\nX", 4));
}

static void response_errors(void)
{
    const char *bad[] = {
        "HTTP/1.1 200 OK\r\nCSeq: 1\r\n\r\n",
        "RTSP/1.0 20 OK\r\nCSeq: 1\r\n\r\n",
        "RTSP/1.0 600 Bad\r\nCSeq: 1\r\n\r\n",
        "RTSP/1.0 200 OK\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: -1\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 4294967296\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\ncseq: 1\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\nContent-Length: -1\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\n folded\r\n\r\n",
        "RTSP/1.0 100 Continue\r\nCSeq: 1\r\nContent-Length: 1\r\n\r\nx",
        "RTSP/1.0 200 OK\nCSeq: 1\n\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        WfdResponse out = {.status = 999};
        size_t used = 123;
        RtspError error;
        assert(wfd_response_parse((const uint8_t *)bad[i], strlen(bad[i]), &out, &used, &error) == RTSP_PARSE_ERROR);
        assert(error == RTSP_ERROR_INVALID_FORMAT && used == 0 && out.status == 999);
    }
    WfdResponse out;
    size_t used;
    RtspError error;
    const char *huge = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nContent-Length: 999999999999999999999\r\n\r\n";
    assert(wfd_response_parse((const uint8_t *)huge, strlen(huge), &out, &used, &error) == RTSP_PARSE_ERROR);
    assert(error == RTSP_ERROR_LIMIT_EXCEEDED);
    uint8_t *header = malloc(RTSP_MAX_HEADER_BYTES);
    assert(header);
    memset(header, 'A', RTSP_MAX_HEADER_BYTES);
    assert(wfd_response_parse(header, RTSP_MAX_HEADER_BYTES, &out, &used, &error) == RTSP_PARSE_ERROR);
    assert(error == RTSP_ERROR_LIMIT_EXCEEDED);
    free(header);
    assert(wfd_response_parse(NULL, 1, &out, &used, &error) == RTSP_PARSE_ERROR);
    assert(error == RTSP_ERROR_INVALID_ARGUMENT);
    assert(wfd_response_parse(NULL, 0, &out, &used, &error) == RTSP_PARSE_NEED_MORE);
    const char *list = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nPublic: PLAY\r\npublic: SETUP\r\n\r\n";
    assert(wfd_response_parse((const uint8_t *)list, strlen(list), &out, &used, &error) == RTSP_PARSE_OK);
    assert(!strcmp(out.public_methods, "PLAY, SETUP"));
    char large[RTSP_MAX_HEADER_VALUE_BYTES + 128];
    const char *prefix = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nPublic: ";
    size_t prefix_len = strlen(prefix);
    memcpy(large, prefix, prefix_len);
    memset(large + prefix_len, 'A', RTSP_MAX_HEADER_VALUE_BYTES);
    const char *suffix = "\r\nPublic: PLAY\r\n\r\n";
    memcpy(large + prefix_len + RTSP_MAX_HEADER_VALUE_BYTES, suffix, strlen(suffix));
    assert(wfd_response_parse((const uint8_t *)large,
                              prefix_len + RTSP_MAX_HEADER_VALUE_BYTES + strlen(suffix),
                              &out, &used, &error) == RTSP_PARSE_ERROR);
    assert(error == RTSP_ERROR_LIMIT_EXCEEDED);
}

static void wire_exchange(void)
{
    const char *m1 = "OPTIONS * RTSP/1.0\r\nCSeq: 55\r\nRequire: org.wfa.wfd1.0\r\n\r\n";
    RtspRequest *request = calloc(1, sizeof(*request));
    assert(request);
    size_t used, written;
    RtspError error;
    assert(rtsp_parse_request((const uint8_t *)m1, strlen(m1), request, &used, &error) == RTSP_PARSE_OK);
    WfdOptions s;
    assert(wfd_options_init(&s, 7, 0, 1000));
    char outgoing[512];
    assert(wfd_options_request(&s, request, 1, outgoing, sizeof(outgoing), &written) == WFD_OPTIONS_OK);
    rtsp_request_clear(request);
    WfdResponse m1_reply;
    assert(wfd_response_parse((const uint8_t *)outgoing, written, &m1_reply, &used, &error) == RTSP_PARSE_OK);
    assert(m1_reply.cseq == 55);
    assert(rtsp_parse_request((const uint8_t *)outgoing + used, written - used, request, &used, &error) == RTSP_PARSE_OK);
    assert(request->cseq == 7 && !strcmp(request->method, "OPTIONS"));
    rtsp_request_clear(request);

    WfdOptionsResult result;
    for (size_t n = 0; n < strlen(reply); ++n) {
        assert(wfd_options_response_bytes(&s, (const uint8_t *)reply, n, 2, &used, &error, &result) == RTSP_PARSE_NEED_MORE);
        assert(s.state == WFD_OPTIONS_WAIT_M2 && used == 0);
    }
    const char *stale = "RTSP/1.0 200 OK\r\nCSeq: 55\r\n\r\n";
    assert(wfd_options_response_bytes(&s, (const uint8_t *)stale, strlen(stale), 2, &used, &error, &result) == RTSP_PARSE_OK);
    assert(result == WFD_OPTIONS_STALE && s.state == WFD_OPTIONS_WAIT_M2);
    const char *bad = "RTSP/1.0 200 OK\r\nCSeq: 7\r\nCSeq: 7\r\n\r\n";
    assert(wfd_options_response_bytes(&s, (const uint8_t *)bad, strlen(bad), 2, &used, &error, &result) == RTSP_PARSE_ERROR);
    assert(s.state == WFD_OPTIONS_WAIT_M2);

    const char *m3 = "GET_PARAMETER rtsp://192.0.2.1/wfd1.0 RTSP/1.0\r\nCSeq: 56\r\n\r\n";
    char input[512];
    memcpy(input, reply, strlen(reply));
    memcpy(input + strlen(reply), m3, strlen(m3));
    assert(wfd_options_response_bytes(&s, (const uint8_t *)input, strlen(reply) + strlen(m3), 3,
                                      &used, &error, &result) == RTSP_PARSE_OK);
    assert(result == WFD_OPTIONS_OK && s.state == WFD_OPTIONS_COMPLETE && used == strlen(reply));
    assert(rtsp_parse_request((const uint8_t *)input + used, strlen(m3), request, &used, &error) == RTSP_PARSE_OK);
    assert(request->cseq == 56 && !strcmp(request->method, "GET_PARAMETER"));
    rtsp_request_clear(request);
    free(request);
}

void test_response(void)
{
    response_prefixes();
    response_errors();
    wire_exchange();
}
