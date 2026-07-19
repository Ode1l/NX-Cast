#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AIRPLAY_DEFAULT_CONTROL_PORT 7000
#define AIRPLAY_FRIENDLY_NAME_MAX 63

typedef enum
{
    AIRPLAY_STATE_STOPPED = 0,
    AIRPLAY_STATE_RUNNING
} AirPlayState;

typedef struct
{
    AirPlayState state;
    uint16_t control_port;
    char friendly_name[AIRPLAY_FRIENDLY_NAME_MAX + 1];
} AirPlayStatus;

typedef struct
{
    AirPlayState previous_state;
    AirPlayState state;
} AirPlayStateEvent;

typedef void (*AirPlayStateCallback)(const AirPlayStateEvent *event, void *user_data);
typedef void (*AirPlayPinDisplayCallback)(const char pin[5], void *user_data);
typedef void (*AirPlayPinDismissCallback)(void *user_data);

typedef struct
{
    const char *friendly_name;
    uint16_t control_port;
    AirPlayStateCallback state_callback;
    void *state_user_data;
    AirPlayPinDisplayCallback pin_display_callback;
    AirPlayPinDismissCallback pin_dismiss_callback;
    void *pin_user_data;
} AirPlayConfig;

/* Lifecycle access remains on the application thread until transport workers add synchronization. */
bool airplay_start(const AirPlayConfig *config);
void airplay_stop(void);
bool airplay_is_running(void);
bool airplay_get_status(AirPlayStatus *status_out);
const char *airplay_state_name(AirPlayState state);
