#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/protocol/rtsp.h"

#define AIRPLAY_SERVER_MAX_CLIENTS 4U
#define AIRPLAY_SERVER_DEFAULT_REQUEST_TIMEOUT_MS 5000U
#define AIRPLAY_SERVER_DEFAULT_SEND_TIMEOUT_MS 3000U

typedef void (*AirPlayServerSessionClosedHandler)(AirPlayRtspSession *session, void *user_data);

typedef struct
{
    uint16_t port;
    uint32_t request_timeout_ms;
    uint32_t send_timeout_ms;
    AirPlayRtspRouteHandler route_handler;
    void *route_user_data;
    AirPlayServerSessionClosedHandler session_closed_handler;
} AirPlayServerConfig;

/* Start and stop are application-thread operations; route callbacks run on client workers. */
bool airplay_server_start(const AirPlayServerConfig *config);
void airplay_server_stop(void);
bool airplay_server_is_running(void);
uint16_t airplay_server_port(void);
size_t airplay_server_active_clients(void);
