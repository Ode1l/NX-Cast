#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTSP_MAX_HEADER_BYTES (32U * 1024U)
#define RTSP_MAX_BODY_BYTES (1024U * 1024U)
#define RTSP_MAX_MESSAGE_BYTES (RTSP_MAX_HEADER_BYTES + RTSP_MAX_BODY_BYTES)
#define RTSP_MAX_HEADERS 48U
#define RTSP_MAX_METHOD_BYTES 31U
#define RTSP_MAX_URI_BYTES 1023U
#define RTSP_MAX_PROTOCOL_BYTES 15U
#define RTSP_MAX_HEADER_NAME_BYTES 63U
#define RTSP_MAX_HEADER_VALUE_BYTES 2047U

typedef enum
{
    RTSP_PARSE_OK = 0,
    RTSP_PARSE_NEED_MORE,
    RTSP_PARSE_ERROR
} RtspParseResult;

typedef enum
{
    RTSP_ERROR_NONE = 0,
    RTSP_ERROR_INVALID_ARGUMENT,
    RTSP_ERROR_INVALID_FORMAT,
    RTSP_ERROR_LIMIT_EXCEEDED,
    RTSP_ERROR_OUT_OF_MEMORY
} RtspError;

typedef struct
{
    char name[RTSP_MAX_HEADER_NAME_BYTES + 1U];
    char value[RTSP_MAX_HEADER_VALUE_BYTES + 1U];
} RtspHeader;

typedef struct
{
    char method[RTSP_MAX_METHOD_BYTES + 1U];
    char uri[RTSP_MAX_URI_BYTES + 1U];
    char protocol[RTSP_MAX_PROTOCOL_BYTES + 1U];
    RtspHeader headers[RTSP_MAX_HEADERS];
    size_t header_count;
    uint8_t *body;
    size_t body_length;
    bool has_cseq;
    uint32_t cseq;
} RtspRequest;

/* RTSP/1.0 and HTTP/1.1 requests; body ownership transfers on PARSE_OK.
 * Clear a successful request before reusing its storage. On incomplete/error
 * input, request_out is unchanged and no body allocation is transferred. */
RtspParseResult rtsp_parse_request(const uint8_t *bytes, size_t length,
                                   RtspRequest *request_out,
                                   size_t *consumed_out, RtspError *error_out);
void rtsp_request_clear(RtspRequest *request);
const char *rtsp_request_header(const RtspRequest *request, const char *name);
