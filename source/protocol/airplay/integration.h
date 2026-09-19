#ifndef NXCAST_AIRPLAY_INTEGRATION_H
#define NXCAST_AIRPLAY_INTEGRATION_H

#include <stdbool.h>

#include "player/core/ownership.h"

#define AIRPLAY_INTEGRATION_PIN_SIZE 5u
#define AIRPLAY_INTEGRATION_STATUS_MAX 96u

typedef struct
{
    bool running;
    bool starting;
    bool pin_visible;
    char pin[AIRPLAY_INTEGRATION_PIN_SIZE];
    char status[AIRPLAY_INTEGRATION_STATUS_MAX];
} AirPlayIntegrationStatus;

bool airplay_integration_start(void);
void airplay_integration_request_stop(void);
/* Stops the current AirPlay media session without taking discovery offline. */
void airplay_integration_stop_active_media(void);
/* Synchronously releases one exact AirPlay owner for cross-protocol takeover. */
bool airplay_integration_release_active_media(
    const PlayerOwnershipLease *lease);
/* Drives bounded cleanup after the final logical control connection closes. */
void airplay_integration_stop(void);
/* Snapshot reads are non-blocking so AirPlay cannot stall the main loop. */
bool airplay_integration_get_status(AirPlayIntegrationStatus *status_out);

#endif
