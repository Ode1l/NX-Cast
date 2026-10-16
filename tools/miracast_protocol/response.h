#pragma once

#include "protocol/rtsp/request.h"

typedef struct {
    unsigned status;
    uint32_t cseq;
    char public_methods[RTSP_MAX_HEADER_VALUE_BYTES + 1];
    const uint8_t *body;
    size_t body_length;
} WfdResponse;

/* Parse exactly one RTSP/1.0 response. CSeq is mandatory. No allocations.
 * body borrows input until it is moved/freed. Output is unchanged on incomplete
 * or error; consumed is zero. Trailing messages are not inspected/consumed.
 * Folded headers and duplicate CSeq/Content-Length are rejected. Multiple Public
 * fields are combined in wire order, within the same bounded value capacity. */
RtspParseResult wfd_response_parse(const uint8_t *bytes, size_t length,
                                   WfdResponse *out, size_t *consumed,
                                   RtspError *error);
