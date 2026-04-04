#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

typedef enum
{
    PLAYER_BACKEND_AUTO = 0,
    PLAYER_BACKEND_MOCK,
    PLAYER_BACKEND_LIBMPV
} PlayerBackendType;

bool player_set_backend(PlayerBackendType backend);
PlayerBackendType player_get_backend(void);
const char *player_get_backend_name(void);

bool player_init(void);
void player_deinit(void);

void player_set_event_callback(PlayerEventCallback callback, void *user);

bool player_set_media(const PlayerMedia *media);
bool player_set_uri(const char *uri, const char *metadata);
bool player_set_uri_with_context(const char *uri, const char *metadata, const PlayerOpenContext *ctx);
bool player_play(void);
bool player_pause(void);
bool player_stop(void);
bool player_seek_ms(int position_ms);
bool player_set_volume(int volume_0_100);
bool player_set_mute(bool mute);
bool player_video_supported(void);
bool player_video_attach_sw(void);
void player_video_detach(void);
bool player_video_render_sw(void *pixels, int width, int height, size_t stride);

int player_get_position_ms(void);
int player_get_duration_ms(void);
int player_get_volume(void);
bool player_get_mute(void);
bool player_is_seekable(void);
PlayerState player_get_state(void);
bool player_get_current_media(PlayerMedia *out);
bool player_get_snapshot(PlayerSnapshot *out);
