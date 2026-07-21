#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_RTSP_MAX_HEADER_BYTES (32U * 1024U)
#define AIRPLAY_RTSP_MAX_BODY_BYTES (1024U * 1024U)
#define AIRPLAY_RTSP_MAX_MESSAGE_BYTES (AIRPLAY_RTSP_MAX_HEADER_BYTES + AIRPLAY_RTSP_MAX_BODY_BYTES)
#define AIRPLAY_RTSP_MAX_HEADERS 48U
#define AIRPLAY_RTSP_MAX_METHOD_BYTES 31U
#define AIRPLAY_RTSP_MAX_URI_BYTES 1023U
#define AIRPLAY_RTSP_MAX_PROTOCOL_BYTES 15U
#define AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES 63U
#define AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES 2047U

typedef enum
{
    AIRPLAY_RTSP_PARSE_OK = 0,
    AIRPLAY_RTSP_PARSE_NEED_MORE,
    AIRPLAY_RTSP_PARSE_ERROR
} AirPlayRtspParseResult;

typedef enum
{
    AIRPLAY_RTSP_ERROR_NONE = 0,
    AIRPLAY_RTSP_ERROR_INVALID_ARGUMENT,
    AIRPLAY_RTSP_ERROR_INVALID_FORMAT,
    AIRPLAY_RTSP_ERROR_LIMIT_EXCEEDED,
    AIRPLAY_RTSP_ERROR_OUT_OF_MEMORY
} AirPlayRtspError;

typedef struct
{
    char name[AIRPLAY_RTSP_MAX_HEADER_NAME_BYTES + 1U];
    char value[AIRPLAY_RTSP_MAX_HEADER_VALUE_BYTES + 1U];
} AirPlayRtspHeader;

typedef struct
{
    char method[AIRPLAY_RTSP_MAX_METHOD_BYTES + 1U];
    char uri[AIRPLAY_RTSP_MAX_URI_BYTES + 1U];
    char protocol[AIRPLAY_RTSP_MAX_PROTOCOL_BYTES + 1U];
    AirPlayRtspHeader headers[AIRPLAY_RTSP_MAX_HEADERS];
    size_t header_count;
    uint8_t *body;
    size_t body_length;
    bool has_cseq;
    uint32_t cseq;
} AirPlayRtspRequest;

typedef struct
{
    char protocol[AIRPLAY_RTSP_MAX_PROTOCOL_BYTES + 1U];
    int status_code;
    char reason[64];
    AirPlayRtspHeader headers[AIRPLAY_RTSP_MAX_HEADERS];
    size_t header_count;
    uint8_t *body;
    size_t body_length;
    bool close_connection;
} AirPlayRtspResponse;

typedef enum
{
    AIRPLAY_RTSP_SESSION_CONNECTED = 0,
    AIRPLAY_RTSP_SESSION_SETUP,
    AIRPLAY_RTSP_SESSION_RECORDING,
    AIRPLAY_RTSP_SESSION_CLOSED
} AirPlayRtspSessionState;

typedef struct
{
    uint64_t id;
    uint32_t request_count;
    uint32_t peer_ipv4_address;
    AirPlayRtspSessionState state;
    void *security_context;
    void *protocol_context;
} AirPlayRtspSession;

typedef bool (*AirPlayRtspRouteHandler)(AirPlayRtspSession *session,
                                        const AirPlayRtspRequest *request,
                                        AirPlayRtspResponse *response,
                                        void *user_data);

AirPlayRtspParseResult airplay_rtsp_parse_request(const uint8_t *bytes,
                                                  size_t length,
                                                  AirPlayRtspRequest *request_out,
                                                  size_t *consumed_out,
                                                  AirPlayRtspError *error_out);
/* Clear each successfully parsed request before reusing or releasing its storage. */
void airplay_rtsp_request_clear(AirPlayRtspRequest *request);
const char *airplay_rtsp_request_header(const AirPlayRtspRequest *request, const char *name);

bool airplay_rtsp_response_init(AirPlayRtspResponse *response, const char *protocol, int status_code);
bool airplay_rtsp_response_set_status(AirPlayRtspResponse *response, int status_code);
bool airplay_rtsp_response_add_header(AirPlayRtspResponse *response,
                                      const char *name,
                                      const char *value);
bool airplay_rtsp_response_set_body(AirPlayRtspResponse *response,
                                    const void *body,
                                    size_t body_length,
                                    const char *content_type);
bool airplay_rtsp_response_encode(const AirPlayRtspResponse *response,
                                  uint8_t **bytes_out,
                                  size_t *length_out);
void airplay_rtsp_response_clear(AirPlayRtspResponse *response);

void airplay_rtsp_session_init(AirPlayRtspSession *session, uint64_t id);
void airplay_rtsp_session_set_peer_ipv4(AirPlayRtspSession *session,
                                        uint32_t address);
bool airplay_rtsp_dispatch(AirPlayRtspSession *session,
                           const AirPlayRtspRequest *request,
                           AirPlayRtspRouteHandler handler,
                           void *user_data,
                           AirPlayRtspResponse *response_out);
bool airplay_rtsp_default_route(AirPlayRtspSession *session,
                                const AirPlayRtspRequest *request,
                                AirPlayRtspResponse *response);
const char *airplay_rtsp_error_name(AirPlayRtspError error);
const char *airplay_rtsp_session_state_name(AirPlayRtspSessionState state);
