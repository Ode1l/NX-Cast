#pragma once

#include "options.h"

#define WFD_PARAMETER_BODY_MAX 4096U

typedef enum { WFD_AUDIO_NONE, WFD_AUDIO_LPCM, WFD_AUDIO_AAC } WfdAudio;

typedef struct {
    uint32_t cea_mode;
    WfdAudio audio;
    char presentation_url[128];
    unsigned received;
    bool complete;
} WfdSelection;

typedef struct {
    uint16_t rtp_port;
    char expected_url[128];
    WfdSelection selection;
    bool initialized;
} WfdParameters;

/* LAB ONLY: H.264 CBP/level bit 0, CEA bits 0 and 5, LPCM48/AAC stereo.
 * No actual decoder/port readiness is inferred. The future runtime must bind
 * the UDP port first. source_ip pins the control peer's IPv4 presentation URL.
 * Initialize once per connection, only after OPTIONS completes. */
bool wfd_parameters_init(WfdParameters *session, const WfdOptions *options,
                          const uint8_t source_ip[4], uint16_t rtp_port);

/* Accepts only successful unmodified common-parser requests. Returns false for
 * local misuse or insufficient output; written=0 and state/output unchanged.
 * Otherwise emits an RTSP response, including peer errors. Only a successfully
 * serialized 200 SET_PARAMETER commits the candidate. No allocations or I/O.
 * Intended only for negotiation: the owner must stop routing here after SETUP. */
bool wfd_parameters_request(WfdParameters *session, const RtspRequest *request,
                             char *output, size_t capacity, size_t *written);
