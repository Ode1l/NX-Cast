#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/airplay/protocol/rtsp.h"

// Shares one AirPlay media identity across its related TCP connections.

#define AIRPLAY_SESSION_APPLE_ID_MAX 128U
#define AIRPLAY_SESSION_CLIENT_ID_SIZE 32U
#define AIRPLAY_SESSION_MAX_CONNECTIONS 8U
#define AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS 8U

typedef enum
{
    AIRPLAY_CONNECTION_UNKNOWN = 0,
    AIRPLAY_CONNECTION_RAOP,
    AIRPLAY_CONNECTION_AIRPLAY,
    AIRPLAY_CONNECTION_BLE
} AirPlayConnectionKind;

typedef enum
{
    AIRPLAY_SESSION_OBSERVE_OK = 0,
    AIRPLAY_SESSION_OBSERVE_INVALID_ARGUMENT,
    AIRPLAY_SESSION_OBSERVE_INVALID_SESSION_ID,
    AIRPLAY_SESSION_OBSERVE_CONFLICT,
    AIRPLAY_SESSION_OBSERVE_UNBOUND_MEDIA,
    AIRPLAY_SESSION_OBSERVE_CAPACITY
} AirPlaySessionObserveResult;

typedef struct
{
    uint64_t connection_id;
    uint64_t logical_session_id;
    AirPlayConnectionKind connection_kind;
    uint32_t logical_connection_count;
    bool logical_session_bound;
} AirPlaySessionSnapshot;

typedef struct
{
    uint64_t connection_id;
    uint64_t logical_session_id;
    bool logical_session_bound;
    bool last_logical_connection;
    uint64_t terminal_media_session_id;
} AirPlaySessionCloseResult;

typedef struct AirPlaySessionManager AirPlaySessionManager;

AirPlaySessionManager *airplay_session_manager_create(void);
void airplay_session_manager_destroy(AirPlaySessionManager *manager);

AirPlaySessionObserveResult airplay_session_manager_observe(
    AirPlaySessionManager *manager,
    uint64_t connection_id,
    const AirPlayRtspRequest *request,
    AirPlaySessionSnapshot *snapshot_out);

bool airplay_session_manager_close(AirPlaySessionManager *manager,
                                   uint64_t connection_id,
                                   AirPlaySessionCloseResult *result_out);

bool airplay_session_manager_bind_client(
    AirPlaySessionManager *manager,
    uint64_t connection_id,
    const uint8_t client_id[AIRPLAY_SESSION_CLIENT_ID_SIZE]);
bool airplay_session_manager_mark_transport_teardown(
    AirPlaySessionManager *manager,
    uint64_t connection_id,
    uint64_t *terminal_media_session_id_out);

bool airplay_session_manager_retain_media(AirPlaySessionManager *manager,
                                          uint64_t logical_session_id);
bool airplay_session_manager_release_media(AirPlaySessionManager *manager,
                                           uint64_t logical_session_id);

bool airplay_session_request_is_remote_video(const AirPlayRtspRequest *request);
bool airplay_session_request_requires_logical_binding(
    const AirPlayRtspRequest *request);
const char *airplay_connection_kind_name(AirPlayConnectionKind kind);
const char *airplay_session_observe_result_name(AirPlaySessionObserveResult result);
