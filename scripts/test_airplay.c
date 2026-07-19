#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/airplay.h"

typedef struct
{
    unsigned calls;
    AirPlayStateEvent last_event;
} StateRecorder;

static void record_state(const AirPlayStateEvent *event, void *user_data)
{
    StateRecorder *recorder = user_data;

    assert(event != NULL);
    assert(recorder != NULL);
    recorder->calls++;
    recorder->last_event = *event;
}

static AirPlayConfig make_config(const char *friendly_name, StateRecorder *recorder)
{
    AirPlayConfig config = {
        .friendly_name = friendly_name,
        .control_port = AIRPLAY_DEFAULT_CONTROL_PORT,
        .state_callback = record_state,
        .state_user_data = recorder
    };
    return config;
}

static void test_invalid_config(void)
{
    StateRecorder recorder = {0};
    AirPlayConfig config = make_config("NX-Cast", &recorder);
    char long_name[AIRPLAY_FRIENDLY_NAME_MAX + 2];

    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    assert(!airplay_start(NULL));
    config.friendly_name = NULL;
    assert(!airplay_start(&config));
    config.friendly_name = "";
    assert(!airplay_start(&config));
    config.friendly_name = long_name;
    assert(!airplay_start(&config));
    config.friendly_name = "NX-Cast";
    config.control_port = 0;
    assert(!airplay_start(&config));
    assert(!airplay_is_running());
    assert(recorder.calls == 0);
}

static void test_idempotent_lifecycle(void)
{
    StateRecorder recorder = {0};
    AirPlayConfig config = make_config("NX-Cast", &recorder);
    AirPlayStatus status;

    assert(airplay_start(&config));
    assert(airplay_is_running());
    assert(airplay_get_status(&status));
    assert(status.state == AIRPLAY_STATE_RUNNING);
    assert(status.control_port == AIRPLAY_DEFAULT_CONTROL_PORT);
    assert(strcmp(status.friendly_name, "NX-Cast") == 0);
    assert(recorder.calls == 1);
    assert(recorder.last_event.previous_state == AIRPLAY_STATE_STOPPED);
    assert(recorder.last_event.state == AIRPLAY_STATE_RUNNING);

    assert(airplay_start(&config));
    assert(recorder.calls == 1);

    airplay_stop();
    assert(!airplay_is_running());
    assert(airplay_get_status(&status));
    assert(status.state == AIRPLAY_STATE_STOPPED);
    assert(status.control_port == 0);
    assert(status.friendly_name[0] == '\0');
    assert(recorder.calls == 2);
    assert(recorder.last_event.previous_state == AIRPLAY_STATE_RUNNING);
    assert(recorder.last_event.state == AIRPLAY_STATE_STOPPED);

    airplay_stop();
    assert(recorder.calls == 2);
}

static void test_callback_isolation(void)
{
    StateRecorder first = {0};
    StateRecorder second = {0};
    AirPlayConfig first_config = make_config("NX-Cast A", &first);
    AirPlayConfig second_config = make_config("NX-Cast B", &second);

    assert(airplay_start(&first_config));
    airplay_stop();
    assert(first.calls == 2);
    assert(second.calls == 0);

    assert(airplay_start(&second_config));
    airplay_stop();
    assert(first.calls == 2);
    assert(second.calls == 2);
}

static void test_config_is_copied_and_callback_is_optional(void)
{
    char friendly_name[] = "NX-Cast Copy";
    AirPlayConfig config = {
        .friendly_name = friendly_name,
        .control_port = AIRPLAY_DEFAULT_CONTROL_PORT,
        .state_callback = NULL,
        .state_user_data = NULL
    };
    AirPlayStatus status;

    assert(airplay_start(&config));
    friendly_name[0] = 'X';
    assert(airplay_get_status(&status));
    assert(strcmp(status.friendly_name, "NX-Cast Copy") == 0);
    airplay_stop();
}

int main(void)
{
    AirPlayStatus status;

    assert(!airplay_is_running());
    assert(!airplay_get_status(NULL));
    assert(airplay_get_status(&status));
    assert(status.state == AIRPLAY_STATE_STOPPED);
    assert(strcmp(airplay_state_name(AIRPLAY_STATE_STOPPED), "stopped") == 0);
    assert(strcmp(airplay_state_name(AIRPLAY_STATE_RUNNING), "running") == 0);
    assert(strcmp(airplay_state_name((AirPlayState)99), "unknown") == 0);

    test_invalid_config();
    test_idempotent_lifecycle();
    test_callback_isolation();
    test_config_is_copied_and_callback_is_optional();

    puts("AirPlay lifecycle tests passed");
    return 0;
}
