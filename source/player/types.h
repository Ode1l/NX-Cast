#pragma once

#include <stdbool.h>

#include "ingress.h"

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

typedef struct
{
    PlayerEventType type;
    PlayerState state;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    bool seekable;
    int error_code;
    const char *uri;
    PlayerMediaProfile media_profile;
} PlayerEvent;

typedef void (*PlayerEventCallback)(const PlayerEvent *event, void *user);

typedef struct
{
    bool has_media;
    PlayerMedia media;
    PlayerState state;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    bool seekable;
} PlayerSnapshot;
