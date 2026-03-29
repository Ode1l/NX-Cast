#pragma once

#include <stdbool.h>

#include "player.h"

typedef struct
{
    const char *name;
    bool (*available)(void);
    bool (*init)(void);
    void (*deinit)(void);
    void (*set_event_sink)(void (*sink)(const PlayerEvent *event));

    bool (*set_uri)(const char *uri, const char *metadata);
    bool (*play)(void);
    bool (*pause)(void);
    bool (*stop)(void);
    bool (*seek_ms)(int position_ms);
    bool (*set_volume)(int volume_0_100);
    bool (*set_mute)(bool mute);

    int (*get_position_ms)(void);
    int (*get_duration_ms)(void);
    int (*get_volume)(void);
    bool (*get_mute)(void);
    PlayerState (*get_state)(void);
} PlayerBackendOps;

extern const PlayerBackendOps g_player_backend_mock;
extern const PlayerBackendOps g_player_backend_libmpv;
