#include "options.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char m1[] =
    "OPTIONS * RTSP/1.0\r\nCSeq: 81\r\n"
    "Require: org.wfa.wfd1.0\r\nContent-Length: 0\r\n\r\n";
static const char methods[] =
    "org.wfa.wfd1.0, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER, SET_PARAMETER";

static RtspRequest *parse(const char *wire)
{
    RtspRequest *r = calloc(1, sizeof(*r));
    size_t consumed = 0;
    RtspError error;
    assert(r);
    assert(rtsp_parse_request((const uint8_t *)wire, strlen(wire), r,
                              &consumed, &error) == RTSP_PARSE_OK);
    assert(consumed == strlen(wire));
    return r;
}

static void release(RtspRequest *r)
{
    rtsp_request_clear(r);
    free(r);
}

static void begin(WfdOptions *s, const RtspRequest *r)
{
    char out[512];
    size_t written;
    assert(wfd_options_init(s, 7, 100, 1000));
    assert(wfd_options_request(s, r, 101, out, sizeof(out), &written) == WFD_OPTIONS_OK);
    assert(s->state == WFD_OPTIONS_WAIT_M2);
}

static void test_wire_and_capacity(void)
{
    RtspRequest *r = parse(m1);
    WfdOptions s;
    char out[512] = "unchanged";
    size_t written = 42;
    assert(wfd_options_init(&s, 7, 100, 1000));
    assert(wfd_options_request(&s, r, 101, out, 1, &written) == WFD_OPTIONS_NO_SPACE);
    assert(!strcmp(out, "unchanged") && written == 0 && s.state == WFD_OPTIONS_WAIT_M1);
    assert(wfd_options_request(&s, r, 101, out, sizeof(out), &written) == WFD_OPTIONS_OK);
    static const char expected[] =
        "RTSP/1.0 200 OK\r\nCSeq: 81\r\n"
        "Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\r\n"
        "Content-Length: 0\r\n\r\n"
        "OPTIONS * RTSP/1.0\r\nCSeq: 7\r\n"
        "Require: org.wfa.wfd1.0\r\nContent-Length: 0\r\n\r\n";
    assert(written == strlen(expected) && !strcmp(out, expected));
    RtspRequest *m2 = parse(strstr(out, "OPTIONS *"));
    assert(m2->cseq == 7);
    release(m2);
    assert(wfd_options_request(&s, r, 102, out, sizeof(out), &written) == WFD_OPTIONS_WRONG_STATE);
    assert(s.state == WFD_OPTIONS_WAIT_M2 && written == 0);
    assert(wfd_options_response(&s, 81, 200, methods, strlen(methods), 102) == WFD_OPTIONS_STALE);
    assert(wfd_options_response(&s, 7, 100, NULL, 0, 103) == WFD_OPTIONS_OK);
    assert(s.state == WFD_OPTIONS_WAIT_M2);
    assert(wfd_options_response(&s, 7, 200, methods, strlen(methods), 104) == WFD_OPTIONS_OK);
    assert(s.state == WFD_OPTIONS_COMPLETE);
    assert(wfd_options_response(&s, 7, 200, methods, strlen(methods), 105) == WFD_OPTIONS_WRONG_STATE);
    wfd_options_close(&s);
    wfd_options_close(&s);
    assert(s.state == WFD_OPTIONS_CLOSED);
    assert(wfd_options_tick(&s, 200) == WFD_OPTIONS_WRONG_STATE);
    release(r);
}

static void test_rejection_and_time(void)
{
    RtspRequest *r = parse(m1);
    WfdOptions s;
    begin(&s, r);
    assert(wfd_options_response(&s, 7, 500, methods, strlen(methods), 102) == WFD_OPTIONS_REJECTED);
    assert(s.state == WFD_OPTIONS_FAILED);
    begin(&s, r);
    const char partial[] = "org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, SETUP, PLAYLIST, TEARDOWN";
    assert(wfd_options_response(&s, 7, 200, partial, strlen(partial), 102) == WFD_OPTIONS_REJECTED);
    begin(&s, r);
    assert(wfd_options_response(&s, 7, 200, NULL, 0, 102) == WFD_OPTIONS_REJECTED);
    begin(&s, r);
    assert(wfd_options_response(&s, 7, 200, methods, RTSP_MAX_HEADER_VALUE_BYTES + 1, 102) == WFD_OPTIONS_INVALID);
    assert(s.state == WFD_OPTIONS_WAIT_M2);
    assert(wfd_options_tick(&s, 100) == WFD_OPTIONS_INVALID);
    assert(wfd_options_tick(&s, 1100) == WFD_OPTIONS_OK);
    assert(wfd_options_response(&s, 7, 200, methods, strlen(methods), 1101) == WFD_OPTIONS_TIMEOUT);
    assert(s.state == WFD_OPTIONS_FAILED);
    assert(wfd_options_init(&s, UINT32_MAX, UINT64_MAX - 10, 10));
    assert(wfd_options_tick(&s, UINT64_MAX - 1) == WFD_OPTIONS_OK);
    assert(wfd_options_tick(&s, UINT64_MAX) == WFD_OPTIONS_TIMEOUT);
    assert(!wfd_options_init(&s, 1, 0, 0));
    begin(&s, r);
    wfd_options_close(&s);
    assert(wfd_options_response(&s, 7, 200, methods, strlen(methods), 102) == WFD_OPTIONS_WRONG_STATE);
    release(r);
}

static void test_parser_and_invalid_requests(void)
{
    RtspRequest *r = calloc(1, sizeof(*r));
    size_t consumed;
    RtspError error;
    assert(r);
    for (size_t i = 0; i < strlen(m1); ++i) {
        assert(rtsp_parse_request((const uint8_t *)m1, i, r, &consumed, &error) == RTSP_PARSE_NEED_MORE);
        assert(!r->body && !r->has_cseq);
    }
    free(r);
    const char *invalid[] = {
        "OPTIONS * HTTP/1.1\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nRequire: org.wfa.wfd1.0\r\n\r\n",
        "GET_PARAMETER * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\nContent-Length: 1\r\n\r\nx",
        "OPTIONS /wrong RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\n\r\n",
        "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\nRequire: other\r\n\r\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        r = parse(invalid[i]);
        WfdOptions s;
        char out[512];
        size_t written;
        assert(wfd_options_init(&s, 1, 0, 1000));
        assert(wfd_options_request(&s, r, 1, out, sizeof(out), &written) == WFD_OPTIONS_INVALID);
        assert(s.state == WFD_OPTIONS_WAIT_M1 && written == 0);
        release(r);
    }
    r = parse("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0, unknown\r\n\r\n");
    WfdOptions s;
    char out[512];
    size_t written;
    assert(wfd_options_init(&s, 1, 0, 1000));
    assert(wfd_options_request(&s, r, 1, out, sizeof(out), &written) == WFD_OPTIONS_REJECTED);
    release(r);
}

static void test_independent_connections(void)
{
    RtspRequest *r = parse(m1);
    WfdOptions a, b;
    char out[512];
    size_t written;
    begin(&a, r);
    assert(wfd_options_init(&b, UINT32_MAX, 100, 1000));
    assert(wfd_options_request(&b, r, 101, out, sizeof(out), &written) == WFD_OPTIONS_OK);
    assert(strstr(out, "CSeq: 4294967295\r\n"));
    size_t exact = written;
    assert(wfd_options_init(&b, UINT32_MAX, 100, 1000));
    assert(wfd_options_request(&b, r, 101, out, exact, &written) == WFD_OPTIONS_NO_SPACE);
    assert(wfd_options_request(&b, r, 101, out, exact + 1, &written) == WFD_OPTIONS_OK);
    wfd_options_close(&a);
    /* Public need not be NUL terminated: do not read the appended junk. */
    char bounded[sizeof(methods) + 8];
    memcpy(bounded, methods, strlen(methods));
    memset(bounded + strlen(methods), 'x', sizeof(bounded) - strlen(methods));
    assert(wfd_options_response(&b, UINT32_MAX, 200, bounded, strlen(methods), 102) == WFD_OPTIONS_OK);
    assert(a.state == WFD_OPTIONS_CLOSED && b.state == WFD_OPTIONS_COMPLETE);
    release(r);
}

void test_response(void);
void test_parameters(void);

int main(void)
{
    test_wire_and_capacity();
    test_rejection_and_time();
    test_parser_and_invalid_requests();
    test_independent_connections();
    test_response();
    test_parameters();
    puts("WFD: M1/M2 wire exchange, M3 capabilities, atomic M4 selection and safety checks passed");
    return 0;
}
