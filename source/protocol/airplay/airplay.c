#include "airplay.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    AirPlayStatus status;
    AirPlayStateCallback state_callback;
    void *state_user_data;
} AirPlayRuntime;

static AirPlayRuntime g_airplay = {
    .status = {
        .state = AIRPLAY_STATE_STOPPED,
        .control_port = 0,
        .friendly_name = ""
    },
    .state_callback = NULL,
    .state_user_data = NULL
};

static bool airplay_friendly_name_valid(const char *friendly_name, size_t *length_out)
{
    size_t length = 0;

    if (!friendly_name)
        return false;

    while (length <= AIRPLAY_FRIENDLY_NAME_MAX && friendly_name[length] != '\0')
        length++;

    if (length == 0 || length > AIRPLAY_FRIENDLY_NAME_MAX)
        return false;

    if (length_out)
        *length_out = length;
    return true;
}

static void airplay_emit_state(AirPlayState previous_state)
{
    AirPlayStateCallback callback = g_airplay.state_callback;
    void *user_data = g_airplay.state_user_data;
    AirPlayStateEvent event = {
        .previous_state = previous_state,
        .state = g_airplay.status.state
    };

    if (callback)
        callback(&event, user_data);
}

bool airplay_start(const AirPlayConfig *config)
{
    size_t friendly_name_length;

    if (!config || config->control_port == 0 ||
        !airplay_friendly_name_valid(config->friendly_name, &friendly_name_length))
    {
        return false;
    }

    if (g_airplay.status.state == AIRPLAY_STATE_RUNNING)
        return true;

    memcpy(g_airplay.status.friendly_name, config->friendly_name, friendly_name_length);
    g_airplay.status.friendly_name[friendly_name_length] = '\0';
    g_airplay.status.control_port = config->control_port;
    g_airplay.state_callback = config->state_callback;
    g_airplay.state_user_data = config->state_user_data;
    g_airplay.status.state = AIRPLAY_STATE_RUNNING;
    airplay_emit_state(AIRPLAY_STATE_STOPPED);
    return true;
}

void airplay_stop(void)
{
    AirPlayStateCallback callback;
    void *user_data;
    AirPlayStateEvent event;

    if (g_airplay.status.state == AIRPLAY_STATE_STOPPED)
        return;

    callback = g_airplay.state_callback;
    user_data = g_airplay.state_user_data;
    event.previous_state = g_airplay.status.state;
    event.state = AIRPLAY_STATE_STOPPED;

    memset(g_airplay.status.friendly_name, 0, sizeof(g_airplay.status.friendly_name));
    g_airplay.status.control_port = 0;
    g_airplay.status.state = AIRPLAY_STATE_STOPPED;
    g_airplay.state_callback = NULL;
    g_airplay.state_user_data = NULL;

    if (callback)
        callback(&event, user_data);
}

bool airplay_is_running(void)
{
    return g_airplay.status.state == AIRPLAY_STATE_RUNNING;
}

bool airplay_get_status(AirPlayStatus *status_out)
{
    if (!status_out)
        return false;

    *status_out = g_airplay.status;
    return true;
}

const char *airplay_state_name(AirPlayState state)
{
    switch (state)
    {
    case AIRPLAY_STATE_STOPPED:
        return "stopped";
    case AIRPLAY_STATE_RUNNING:
        return "running";
    default:
        return "unknown";
    }
}
