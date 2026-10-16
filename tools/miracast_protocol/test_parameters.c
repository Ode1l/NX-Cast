#include "parameters.h"
#include "response.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char video[] = "wfd_video_formats: 00 00 01 01 00000020 00000000 00000000 00 0000 0000 00 none none\r\n";
static const char rest[] =
    "wfd_audio_codecs: AAC 00000001 00\r\n"
    "wfd_presentation_URL: rtsp://192.0.2.1/wfd1.0/streamid=0 none\r\n"
    "wfd_client_rtp_ports: RTP/AVP/UDP;unicast 19000 0 mode=play\r\n";

static RtspRequest *request(const char *method, const char *body, size_t length)
{
    char wire[WFD_PARAMETER_BODY_MAX + 512];
    int n = snprintf(wire, sizeof(wire), "%s rtsp://localhost/wfd1.0 RTSP/1.0\r\n"
                     "CSeq: 18\r\nContent-Type: text/parameters\r\nContent-Length: %zu\r\n\r\n", method, length);
    assert(n > 0 && (size_t)n + length <= sizeof(wire));
    memcpy(wire + n, body, length);
    RtspRequest *r = calloc(1, sizeof(*r));
    size_t used;
    assert(r);
    assert(rtsp_parse_request((const uint8_t *)wire, (size_t)n + length, r, &used, NULL) == RTSP_PARSE_OK);
    assert(used == (size_t)n + length);
    return r;
}

static unsigned exchange(WfdParameters *s, const char *method, const char *body,
                          char *response, size_t capacity)
{
    RtspRequest *r = request(method, body, strlen(body));
    size_t written, used;
    assert(wfd_parameters_request(s, r, response, capacity, &written));
    WfdResponse decoded;
    assert(wfd_response_parse((const uint8_t *)response, written, &decoded, &used, NULL) == RTSP_PARSE_OK);
    assert(used == written && decoded.cseq == 18);
    rtsp_request_clear(r);
    free(r);
    return decoded.status;
}

static WfdParameters initialize(void)
{
    WfdOptions options;
    WfdParameters result;
    const uint8_t ip[] = {192, 0, 2, 1};
    assert(wfd_options_init(&options, 7, 0, 1000));
    assert(!wfd_parameters_init(&result, &options, ip, 19000));
    const char *m1 = "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\n\r\n";
    RtspRequest *r = calloc(1, sizeof(*r));
    assert(r);
    size_t used, written;
    assert(rtsp_parse_request((const uint8_t *)m1, strlen(m1), r, &used, NULL) == RTSP_PARSE_OK);
    char out[512];
    assert(wfd_options_request(&options, r, 1, out, sizeof(out), &written) == WFD_OPTIONS_OK);
    rtsp_request_clear(r);
    free(r);
    const char *m2 = "RTSP/1.0 200 OK\r\nCSeq: 7\r\nPublic: org.wfa.wfd1.0, SETUP, PLAY, TEARDOWN, GET_PARAMETER, SET_PARAMETER\r\n\r\n";
    WfdOptionsResult status;
    assert(wfd_options_response_bytes(&options, (const uint8_t *)m2, strlen(m2), 2, &used, NULL, &status) == RTSP_PARSE_OK);
    assert(status == WFD_OPTIONS_OK);
    assert(!wfd_parameters_init(&result, &options, ip, 0));
    assert(wfd_parameters_init(&result, &options, ip, 19000));
    return result;
}

void test_parameters(void)
{
    WfdParameters s = initialize();
    char out[8192];
    const char *query = "wfd_video_formats\r\nwfd_audio_codecs\r\nwfd_client_rtp_ports\r\nwfd_content_protection\r\n";
    assert(exchange(&s, "GET_PARAMETER", query, out, sizeof(out)) == 200);
    assert(strstr(out, "wfd_video_formats: 28 00 01 01 00000021"));
    assert(strstr(out, "LPCM 00000002 00, AAC 00000001 00"));
    assert(strstr(out, "19000 0 mode=play") && strstr(out, "wfd_content_protection: none\r\n"));
    assert(!strstr(out, "wfd_display_edid") && !s.selection.complete);
    assert(exchange(&s, "GET_PARAMETER", "", out, sizeof(out)) == 200);
    assert(exchange(&s, "GET_PARAMETER", "wfd_unknown\r\n", out, sizeof(out)) == 451);
    assert(exchange(&s, "GET_PARAMETER", "wfd_video_formats: value\r\n", out, sizeof(out)) == 400);
    assert(exchange(&s, "GET_PARAMETER", "wfd_audio_codecs\r\nwfd_audio_codecs\r\n", out, sizeof(out)) == 400);

    assert(exchange(&s, "SET_PARAMETER", video, out, sizeof(out)) == 200);
    assert(!s.selection.complete && s.selection.cea_mode == 0x20);
    assert(exchange(&s, "SET_PARAMETER", rest, out, sizeof(out)) == 200);
    assert(s.selection.complete && s.selection.audio == WFD_AUDIO_AAC);
    assert(!strcmp(s.selection.presentation_url, "rtsp://192.0.2.1/wfd1.0/streamid=0"));
    WfdSelection before = s.selection;
    const char *bad[] = {
        "wfd_audio_codecs: LPCM 00000002 00\r\nwfd_client_rtp_ports: RTP/AVP/UDP;unicast 19002 0 mode=play\r\n",
        "wfd_audio_codecs: AC3 00000001 00\r\n",
        "wfd_presentation_URL: rtsp://192.0.2.2/wfd1.0/streamid=0 none\r\n",
        "wfd_content_protection: HDCP2.1 port=1024\r\n",
        "wfd_video_formats: 00 00 01 01 00000080 00000000 00000000 00 0000 0000 00 none none\r\n",
        "wfd_video_formats: 00 00 01 01 00000021 00000000 00000000 00 0000 0000 00 none none\r\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(exchange(&s, "SET_PARAMETER", bad[i], out, sizeof(out)) == 406);
        assert(!memcmp(&s.selection, &before, sizeof(before)));
    }
    assert(exchange(&s, "SET_PARAMETER", "wfd_trigger_method: SETUP\r\n", out, sizeof(out)) == 451);
    assert(exchange(&s, "SET_PARAMETER", "wfd_audio_codecs: AAC 1 00\r\n", out, sizeof(out)) == 400);
    assert(exchange(&s, "SET_PARAMETER", "wfd_audio_codecs: AAC 00000001 00", out, sizeof(out)) == 400);
    assert(!memcmp(&s.selection, &before, sizeof(before)));

    RtspRequest *r = request("SET_PARAMETER", "wfd_audio_codecs: none\r\n", strlen("wfd_audio_codecs: none\r\n"));
    size_t written;
    strcpy(out, "unchanged");
    assert(!wfd_parameters_request(&s, r, out, 1, &written));
    assert(!written && !strcmp(out, "unchanged") && !memcmp(&s.selection, &before, sizeof(before)));
    r->body[3] = 0;
    assert(wfd_parameters_request(&s, r, out, sizeof(out), &written));
    assert(strstr(out, "RTSP/1.0 400") && !memcmp(&s.selection, &before, sizeof(before)));
    rtsp_request_clear(r);
    free(r);
    assert(exchange(&s, "SET_PARAMETER", "wfd_client_rtp_ports: RTP/AVP/UDP;unicast\t019000 0 mode=play\r\n", out, sizeof(out)) == 200);
    assert(exchange(&s, "SET_PARAMETER", "wfd_audio_codecs: LPCM 00000002 00\r\n", out, sizeof(out)) == 200);
    assert(s.selection.audio == WFD_AUDIO_LPCM);

    WfdParameters fresh = initialize();
    char full[512];
    snprintf(full, sizeof(full), "%s%s", video, rest);
    assert(exchange(&fresh, "SET_PARAMETER", full, out, sizeof(out)) == 200);
    assert(fresh.selection.complete);
    WfdSelection selected = fresh.selection;
    assert(exchange(&fresh, "SET_PARAMETER", full, out, sizeof(out)) == 200);
    assert(!memcmp(&selected, &fresh.selection, sizeof(selected)));
    assert(exchange(&fresh, "SET_PARAMETER", "wfd_audio_codecs: none\r\n", out, sizeof(out)) == 200);
    assert(fresh.selection.complete && fresh.selection.audio == WFD_AUDIO_NONE);
    assert(exchange(&fresh, "SET_PARAMETER", "wfd_client_rtp_ports: RTP/AVP/UDP;unicast 99999999999 0 mode=play\r\n", out, sizeof(out)) == 400);
    assert(exchange(&fresh, "SET_PARAMETER", "wfd_audio_codecs: none\r\nwfd_audio_codecs: none\r\n", out, sizeof(out)) == 400);
    assert(exchange(&fresh, "PLAY", "", out, sizeof(out)) == 405);
    assert(exchange(&fresh, "SET_PARAMETER", "", out, sizeof(out)) == 400);
    char oversized[WFD_PARAMETER_BODY_MAX + 2];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    assert(exchange(&fresh, "GET_PARAMETER", oversized, out, sizeof(out)) == 413);
    r = request("GET_PARAMETER", query, strlen(query));
    for (size_t i = 0; i < r->header_count; ++i)
        if (!strcmp(r->headers[i].name, "Content-Type")) strcpy(r->headers[i].value, "application/json");
    assert(wfd_parameters_request(&fresh, r, out, sizeof(out), &written));
    assert(strstr(out, "RTSP/1.0 415"));
    rtsp_request_clear(r);
    free(r);
}
