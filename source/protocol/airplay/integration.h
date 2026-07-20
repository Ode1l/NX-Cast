#ifndef NXCAST_AIRPLAY_INTEGRATION_H
#define NXCAST_AIRPLAY_INTEGRATION_H

#include <stdbool.h>

#define AIRPLAY_INTEGRATION_PIN_SIZE 5u
#define AIRPLAY_INTEGRATION_STATUS_MAX 96u

typedef struct
{
    bool running;
    bool pin_visible;
    char pin[AIRPLAY_INTEGRATION_PIN_SIZE];
    char status[AIRPLAY_INTEGRATION_STATUS_MAX];
} AirPlayIntegrationStatus;

bool airplay_integration_start(void);
bool airplay_integration_start_async(void);
void airplay_integration_stop(void);
bool airplay_integration_get_status(AirPlayIntegrationStatus *status_out);

#endif
