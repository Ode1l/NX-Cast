#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/protocol/rtsp.h"

#define AIRPLAY_REMOTE_HLS_URL_MAX 4095u
#define AIRPLAY_REMOTE_HLS_APPLE_SESSION_MAX 128u
#define AIRPLAY_REMOTE_HLS_LOCAL_URL_MAX 255u

typedef struct AirPlayRemoteHls AirPlayRemoteHls;

typedef struct
{
    uint32_t request_id;
    char apple_session_id[AIRPLAY_REMOTE_HLS_APPLE_SESSION_MAX + 1u];
    uint8_t *body;
    size_t body_length;
} AirPlayRemoteHlsEvent;

typedef enum
{
    AIRPLAY_REMOTE_HLS_ACTION_ACK = 0,
    AIRPLAY_REMOTE_HLS_ACTION_REQUEST_NEXT,
    AIRPLAY_REMOTE_HLS_ACTION_READY
} AirPlayRemoteHlsActionKind;

typedef enum
{
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_OK = 0,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_CONTENT_TYPE,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_INVALID_ARGUMENT,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_PLIST_DECODE,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_SHAPE,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_STATUS,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_REQUEST_ID,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_URL_OR_DATA,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_PLAYLIST,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_SESSION_MISMATCH,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_REQUEST_MISMATCH,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_URL_MISMATCH,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_REWRITE_FAILED,
    AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_STATE
} AirPlayRemoteHlsActionResult;

typedef struct
{
    AirPlayRemoteHlsActionKind kind;
    uint32_t generation;
    AirPlayRemoteHlsEvent event;
    char playback_url[AIRPLAY_REMOTE_HLS_LOCAL_URL_MAX + 1u];
} AirPlayRemoteHlsAction;

bool airplay_remote_hls_create(AirPlayRemoteHls **hls_out);
void airplay_remote_hls_destroy(AirPlayRemoteHls *hls);
bool airplay_remote_hls_locator_supported(const char *locator);
bool airplay_remote_hls_begin(AirPlayRemoteHls *hls,
                              uint64_t session_id,
                              uint32_t generation,
                              uint16_t control_port,
                              const char *apple_session_id,
                              const char *locator,
                              AirPlayRemoteHlsEvent *event_out);
bool airplay_remote_hls_handle_action(AirPlayRemoteHls *hls,
                                      uint64_t session_id,
                                      const uint8_t *body,
                                      size_t body_length,
                                      AirPlayRemoteHlsAction *action_out,
                                      AirPlayRemoteHlsActionResult *result_out);
bool airplay_remote_hls_is_local_uri(const char *uri);
bool airplay_remote_hls_serve(AirPlayRemoteHls *hls,
                              const AirPlayRtspRequest *request,
                              AirPlayRtspResponse *response,
                              bool *handled_out);
void airplay_remote_hls_reset(AirPlayRemoteHls *hls,
                              uint64_t session_id,
                              uint32_t generation);
void airplay_remote_hls_event_clear(AirPlayRemoteHlsEvent *event);
const char *airplay_remote_hls_action_name(AirPlayRemoteHlsActionKind kind);
const char *airplay_remote_hls_action_result_name(
    AirPlayRemoteHlsActionResult result);
