#include "parameters.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { VIDEO = 1, AUDIO = 2, URL = 4, PORT = 8, ALL = 15 };
/* One codec descriptor, baseline experiment; not a measured device capability. */
static const char video_cap[] = "28 00 01 01 00000021 00000000 00000000 00 0000 0000 00 none none";
static const char audio_cap[] = "LPCM 00000002 00, AAC 00000001 00";

typedef struct { char *name; char *value; } Parameter;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    return s;
}

static bool parse_lines(char *body, bool setting, Parameter fields[16], size_t *count)
{
    *count = 0;
    while (*body) {
        char *end = strstr(body, "\r\n");
        if (!end || end == body || *count == 16) return false;
        *end = 0;
        char *colon = strchr(body, ':');
        if (setting != (colon != NULL)) return false;
        char *value = NULL;
        if (colon) { *colon = 0; value = trim(colon + 1); if (!*value) return false; }
        char *name = trim(body);
        if (!*name || strlen(name) > 63) return false;
        for (const char *p = name; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '_')) return false;
        for (size_t i = 0; i < *count; ++i)
            if (!strcmp(fields[i].name, name)) return false;
        fields[(*count)++] = (Parameter){name, value};
        body = end + 2;
    }
    return true;
}

static size_t words(char *value, char **out, size_t max)
{
    size_t n = 0;
    while (*value) {
        while (*value == ' ' || *value == '\t') ++value;
        if (!*value) break;
        if (n == max) return max + 1;
        out[n++] = value;
        while (*value && *value != ' ' && *value != '\t') ++value;
        if (*value) *value++ = 0;
    }
    return n;
}

static bool hex(const char *s, size_t width, uint32_t *out)
{
    if (strlen(s) != width) return false;
    uint32_t n = 0;
    for (size_t i = 0; i < width; ++i) {
        unsigned c = (unsigned char)s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        n = n * 16 + d;
    }
    *out = n;
    return true;
}

static unsigned select_video(char *value, WfdSelection *candidate)
{
    char *part[13];
    uint32_t n[11];
    static const size_t widths[] = {2, 2, 2, 2, 8, 8, 8, 2, 4, 4, 2};
    if (words(value, part, 13) != 13) return 400;
    for (size_t i = 0; i < 11; ++i)
        if (!hex(part[i], widths[i], &n[i])) return 400;
    /* Native and maximum dimensions are ignored for M4, but still syntax checked. */
    uint32_t ignored;
    for (size_t i = 11; i < 13; ++i)
        if (strcmp(part[i], "none") && !hex(part[i], 4, &ignored)) return 400;
    if (n[1] || n[2] != 1 || n[3] != 1 || (n[4] != 1 && n[4] != 0x20) ||
        n[5] || n[6] || n[8] || n[9] || n[10]) return 406;
    candidate->cea_mode = n[4];
    candidate->received |= VIDEO;
    return 200;
}

static unsigned select_audio(char *value, WfdSelection *candidate)
{
    if (!strcmp(value, "none")) candidate->audio = WFD_AUDIO_NONE;
    else {
        char *part[3];
        uint32_t mode, latency;
        if (words(value, part, 3) != 3 || !hex(part[1], 8, &mode) || !hex(part[2], 2, &latency)) return 400;
        if (!strcmp(part[0], "LPCM") && mode == 2) candidate->audio = WFD_AUDIO_LPCM;
        else if (!strcmp(part[0], "AAC") && mode == 1) candidate->audio = WFD_AUDIO_AAC;
        else return 406;
    }
    candidate->received |= AUDIO;
    return 200;
}

static const char *capability(const char *name, const char *ports)
{
    if (!strcmp(name, "wfd_video_formats")) return video_cap;
    if (!strcmp(name, "wfd_audio_codecs")) return audio_cap;
    if (!strcmp(name, "wfd_client_rtp_ports")) return ports;
    static const char *absent[] = {
        "wfd_content_protection", "wfd_display_edid", "wfd_coupled_sink",
        "wfd_uibc_capability", "wfd_3d_video_formats", "wfd_I2C"
    };
    for (size_t i = 0; i < sizeof(absent) / sizeof(absent[0]); ++i)
        if (!strcmp(name, absent[i])) return "none";
    return NULL;
}

static unsigned select_parameter(const Parameter *p, WfdParameters *s,
                                  WfdSelection *candidate)
{
    if (!strcmp(p->name, "wfd_video_formats")) return select_video(p->value, candidate);
    if (!strcmp(p->name, "wfd_audio_codecs")) return select_audio(p->value, candidate);
    if (!strcmp(p->name, "wfd_presentation_URL")) {
        char *part[2];
        if (words(p->value, part, 2) != 2) return 400;
        if (strcmp(part[0], s->expected_url) || strcmp(part[1], "none")) return 406;
        memcpy(candidate->presentation_url, s->expected_url, strlen(s->expected_url) + 1);
        candidate->received |= URL;
        return 200;
    }
    if (!strcmp(p->name, "wfd_client_rtp_ports")) {
        /* Ports are the sink's prior offer, not a new sender-selected endpoint. */
        char *part[4];
        if (words(p->value, part, 4) != 4) return 400;
        unsigned port[2] = {0};
        for (size_t i = 0; i < 2; ++i) {
            if (!*part[i + 1]) return 400;
            for (const char *v = part[i + 1]; *v; ++v) {
                if (*v < '0' || *v > '9') return 400;
                unsigned digit = (unsigned)(*v - '0');
                if (port[i] > (65535U - digit) / 10U) return 400;
                port[i] = port[i] * 10 + digit;
            }
        }
        if (strcmp(part[0], "RTP/AVP/UDP;unicast") || strcmp(part[3], "mode=play") ||
            port[0] != s->rtp_port || port[1] != 0) return 406;
        candidate->received |= PORT;
        return 200;
    }
    if (!strcmp(p->name, "wfd_content_protection"))
        return !strcmp(p->value, "none") ? 200 : 406;
    return 451;
}

bool wfd_parameters_init(WfdParameters *s, const WfdOptions *options,
                          const uint8_t ip[4], uint16_t port)
{
    if (!s || !options || options->state != WFD_OPTIONS_COMPLETE || !ip || !port) return false;
    WfdParameters init = {.rtp_port = port, .initialized = true};
    snprintf(init.expected_url, sizeof(init.expected_url), "rtsp://%u.%u.%u.%u/wfd1.0/streamid=0",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
    *s = init;
    return true;
}

bool wfd_parameters_request(WfdParameters *s, const RtspRequest *r,
                             char *out, size_t cap, size_t *written)
{
    if (written) *written = 0;
    if (!s || !s->initialized || !r || !out || !written || !r->has_cseq) return false;
    bool setting = !strcmp(r->method, "SET_PARAMETER");
    unsigned status = 200;
    WfdSelection candidate = s->selection;
    char input[WFD_PARAMETER_BODY_MAX + 1], body[WFD_PARAMETER_BODY_MAX + 1] = {0};
    size_t body_length = 0;
    char ports[80];
    snprintf(ports, sizeof(ports), "RTP/AVP/UDP;unicast %u 0 mode=play", (unsigned)s->rtp_port);
    if (strcmp(r->protocol, "RTSP/1.0") || strncmp(r->uri, "rtsp://", 7)) status = 400;
    else if (!setting && strcmp(r->method, "GET_PARAMETER")) status = 405;
    else if (r->body_length > WFD_PARAMETER_BODY_MAX) status = 413;
    else if (setting && !r->body_length) status = 400;
    else if (r->body_length) {
        const char *type = NULL;
        for (size_t i = 0; i < r->header_count; ++i) {
            if (!strcasecmp(r->headers[i].name, "Content-Type")) {
                if (type) { status = 400; break; }
                type = r->headers[i].value;
            }
        }
        if (status == 200 && (!type || strcasecmp(type, "text/parameters"))) status = 415;
        for (size_t i = 0; status == 200 && i < r->body_length; ++i) {
            uint8_t c = r->body[i];
            if ((c < 32 && c != '\r' && c != '\n' && c != '\t') || c > 126) status = 400;
        }
        if (status == 200) {
            memcpy(input, r->body, r->body_length);
            input[r->body_length] = 0;
            Parameter fields[16];
            size_t count;
            if (!parse_lines(input, setting, fields, &count)) status = 400;
            else for (size_t i = 0; status == 200 && i < count; ++i) {
                if (setting) status = select_parameter(&fields[i], s, &candidate);
                else {
                    const char *value = capability(fields[i].name, ports);
                    if (!value) { status = 451; break; }
                    int n = snprintf(body + body_length, sizeof(body) - body_length,
                                     "%s: %s\r\n", fields[i].name, value);
                    if (n < 0 || (size_t)n >= sizeof(body) - body_length) { status = 413; break; }
                    body_length += (size_t)n;
                }
            }
        }
    }
    if (status != 200) body_length = 0;
    const char *reason = status == 200 ? "OK" : status == 400 ? "Bad Request" :
        status == 405 ? "Method Not Allowed" : status == 406 ? "Not Acceptable" :
        status == 413 ? "Request Entity Too Large" : status == 415 ? "Unsupported Media Type" :
        "Parameter Not Understood";
    char header[256];
    int n = snprintf(header, sizeof(header),
                     "RTSP/1.0 %u %s\r\nCSeq: %" PRIu32 "\r\n%sContent-Length: %zu\r\n\r\n",
                     status, reason, r->cseq, body_length ? "Content-Type: text/parameters\r\n" : "", body_length);
    if (n < 0 || (size_t)n >= sizeof(header) || cap <= (size_t)n + body_length) return false;
    memcpy(out, header, (size_t)n);
    memcpy(out + n, body, body_length);
    *written = (size_t)n + body_length;
    out[*written] = 0;
    if (status == 200 && setting) {
        candidate.complete = candidate.received == ALL;
        s->selection = candidate;
    }
    return true;
}
