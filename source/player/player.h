#pragma once

#include <stdbool.h>

typedef enum
{
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_LOADING,
    PLAYER_STATE_BUFFERING,
    PLAYER_STATE_SEEKING,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_ERROR
} PlayerState;

typedef enum
{
    PLAYER_EVENT_STATE_CHANGED = 0,
    PLAYER_EVENT_POSITION_CHANGED,
    PLAYER_EVENT_DURATION_CHANGED,
    PLAYER_EVENT_VOLUME_CHANGED,
    PLAYER_EVENT_MUTE_CHANGED,
    PLAYER_EVENT_URI_CHANGED,
    PLAYER_EVENT_ERROR
} PlayerEventType;

typedef enum
{
    PLAYER_BACKEND_AUTO = 0,
    PLAYER_BACKEND_MOCK,
    PLAYER_BACKEND_LIBMPV
} PlayerBackendType;

typedef struct
{
    PlayerEventType type;
    PlayerState state;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    int error_code;
    const char *uri;
} PlayerEvent;

typedef void (*PlayerEventCallback)(const PlayerEvent *event, void *user);

bool player_set_backend(PlayerBackendType backend);
PlayerBackendType player_get_backend(void);
const char *player_get_backend_name(void);

bool player_init(void);
void player_deinit(void);

void player_set_event_callback(PlayerEventCallback callback, void *user);

bool player_set_uri(const char *uri, const char *metadata);
bool player_play(void);
bool player_pause(void);
bool player_stop(void);
bool player_seek_ms(int position_ms);
bool player_set_volume(int volume_0_100);
bool player_set_mute(bool mute);

int player_get_position_ms(void);
int player_get_duration_ms(void);
int player_get_volume(void);
bool player_get_mute(void);
bool player_is_seekable(void);
PlayerState player_get_state(void);
