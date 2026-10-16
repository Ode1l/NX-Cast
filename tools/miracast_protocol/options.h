#pragma once

#include "protocol/rtsp/request.h"

typedef enum {
    WFD_OPTIONS_WAIT_M1,
    WFD_OPTIONS_WAIT_M2,
    WFD_OPTIONS_COMPLETE,
    WFD_OPTIONS_FAILED,
    WFD_OPTIONS_CLOSED
} WfdOptionsState;

typedef enum {
    WFD_OPTIONS_OK,
    WFD_OPTIONS_INVALID,
    WFD_OPTIONS_WRONG_STATE,
    WFD_OPTIONS_NO_SPACE,
    WFD_OPTIONS_STALE,
    WFD_OPTIONS_REJECTED,
    WFD_OPTIONS_TIMEOUT
} WfdOptionsResult;

typedef struct {
    WfdOptionsState state;
    uint32_t local_cseq;
    uint64_t started_ms;
    uint64_t timeout_ms;
} WfdOptions;

/* Single owner, no I/O. A new connection must have its own instance.
 * now_ms is monotonic. timeout_ms must be nonzero. */
bool wfd_options_init(WfdOptions *session, uint32_t local_cseq,
                      uint64_t now_ms, uint64_t timeout_ms);

/* request must be an unmodified successful rtsp_parse_request result.
 * Emits M1 reply followed by M2 request into caller-owned storage. The caller
 * queues all written bytes in order, handles partial sends, and closes on send
 * failure. Short output leaves state unchanged; written is always initialized.
 * This laboratory exchange advertises the later WFD methods, not a runnable
 * receiver: do not expose it on a network before implementing those methods. */
WfdOptionsResult wfd_options_request(WfdOptions *session,
                                     const RtspRequest *request,
                                     uint64_t now_ms, char *output,
                                     size_t capacity, size_t *written);

/* Transport must parse an RTSP/1.0 response, reject duplicate/malformed CSeq
 * and Public fields, and pass a bounded Public span (not necessarily terminated).
 * 1xx responses keep waiting. COMPLETE means OPTIONS only, never media ready. */
WfdOptionsResult wfd_options_response(WfdOptions *session, uint32_t cseq,
                                      unsigned status, const char *public_value,
                                      size_t public_length, uint64_t now_ms);
WfdOptionsResult wfd_options_tick(WfdOptions *session, uint64_t now_ms);
void wfd_options_close(WfdOptions *session);
