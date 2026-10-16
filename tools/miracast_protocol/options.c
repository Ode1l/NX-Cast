#include "options.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool has_token(const char *list, size_t length, const char *token)
{
    size_t start = 0;
    size_t wanted = strlen(token);
    while (start < length) {
        size_t end = start;
        while (end < length && list[end] != ',') ++end;
        size_t left = start, right = end;
        while (left < right && (list[left] == ' ' || list[left] == '\t')) ++left;
        while (right > left && (list[right - 1] == ' ' || list[right - 1] == '\t')) --right;
        if (right - left == wanted && !memcmp(list + left, token, wanted)) return true;
        start = end + 1;
    }
    return false;
}

bool wfd_options_init(WfdOptions *s, uint32_t cseq, uint64_t now, uint64_t timeout)
{
    if (!s || !timeout) return false;
    *s = (WfdOptions){WFD_OPTIONS_WAIT_M1, cseq, now, timeout};
    return true;
}

WfdOptionsResult wfd_options_tick(WfdOptions *s, uint64_t now)
{
    if (!s || now < s->started_ms) return WFD_OPTIONS_INVALID;
    if (s->state != WFD_OPTIONS_WAIT_M1 && s->state != WFD_OPTIONS_WAIT_M2)
        return WFD_OPTIONS_WRONG_STATE;
    /* Subtraction avoids deadline overflow near UINT64_MAX. */
    if (now - s->started_ms >= s->timeout_ms) {
        s->state = WFD_OPTIONS_FAILED;
        return WFD_OPTIONS_TIMEOUT;
    }
    return WFD_OPTIONS_OK;
}

WfdOptionsResult wfd_options_request(WfdOptions *s, const RtspRequest *r,
                                     uint64_t now, char *out, size_t cap,
                                     size_t *written)
{
    if (written) *written = 0;
    if (!s || !r || !out || !written) return WFD_OPTIONS_INVALID;
    if (s->state != WFD_OPTIONS_WAIT_M1) return WFD_OPTIONS_WRONG_STATE;
    WfdOptionsResult result = wfd_options_tick(s, now);
    if (result != WFD_OPTIONS_OK) return result;
    if (strcmp(r->protocol, "RTSP/1.0") || strcmp(r->method, "OPTIONS") ||
        strcmp(r->uri, "*") || !r->has_cseq || r->body_length)
        return WFD_OPTIONS_INVALID;

    /* Require is an extension list. This prototype supports only WFD 1.0;
     * do not silently accept an unknown required extension or duplicate field. */
    const char *require = NULL;
    for (size_t i = 0; i < r->header_count; ++i) {
        if (!strcasecmp(r->headers[i].name, "Require")) {
            if (require) return WFD_OPTIONS_INVALID;
            require = r->headers[i].value;
        }
    }
    if (!require) return WFD_OPTIONS_INVALID;
    size_t length = strlen(require);
    while (length && (*require == ' ' || *require == '\t')) { ++require; --length; }
    while (length && (require[length - 1] == ' ' || require[length - 1] == '\t')) --length;
    if (length != strlen("org.wfa.wfd1.0") || memcmp(require, "org.wfa.wfd1.0", length))
        return WFD_OPTIONS_REJECTED;

    char wire[512];
    int n = snprintf(wire, sizeof(wire),
        "RTSP/1.0 200 OK\r\nCSeq: %" PRIu32 "\r\n"
        "Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\r\n"
        "Content-Length: 0\r\n\r\n"
        "OPTIONS * RTSP/1.0\r\nCSeq: %" PRIu32 "\r\n"
        "Require: org.wfa.wfd1.0\r\nContent-Length: 0\r\n\r\n",
        r->cseq, s->local_cseq);
    if (n < 0 || (size_t)n >= sizeof(wire)) return WFD_OPTIONS_INVALID;
    if (cap <= (size_t)n) return WFD_OPTIONS_NO_SPACE;
    memcpy(out, wire, (size_t)n + 1);
    *written = (size_t)n;
    s->state = WFD_OPTIONS_WAIT_M2;
    s->started_ms = now;
    return WFD_OPTIONS_OK;
}

WfdOptionsResult wfd_options_response(WfdOptions *s, uint32_t cseq,
                                      unsigned status, const char *value,
                                      size_t length, uint64_t now)
{
    if (!s || (!value && length) || length > RTSP_MAX_HEADER_VALUE_BYTES ||
        status < 100 || status > 599) return WFD_OPTIONS_INVALID;
    if (s->state != WFD_OPTIONS_WAIT_M2) return WFD_OPTIONS_WRONG_STATE;
    WfdOptionsResult result = wfd_options_tick(s, now);
    if (result != WFD_OPTIONS_OK) return result;
    if (cseq != s->local_cseq) return WFD_OPTIONS_STALE;
    if (status < 200) return WFD_OPTIONS_OK;
    static const char *required[] = {
        "org.wfa.wfd1.0", "GET_PARAMETER", "SET_PARAMETER", "SETUP", "PLAY", "TEARDOWN"
    };
    bool supported = status == 200;
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        supported = supported && has_token(value, length, required[i]);
    s->state = supported ? WFD_OPTIONS_COMPLETE : WFD_OPTIONS_FAILED;
    return supported ? WFD_OPTIONS_OK : WFD_OPTIONS_REJECTED;
}

void wfd_options_close(WfdOptions *s)
{
    if (s) s->state = WFD_OPTIONS_CLOSED;
}
