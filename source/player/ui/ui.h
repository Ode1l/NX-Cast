#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "player/player.h"

typedef struct
{
    bool video_active;
    PlayerState last_state;
    uint64_t overlay_until_ms;
    uint64_t overlay_refresh_at_ms;
} PlayerUiState;

#define PLAYER_UI_SEEK_STEP_MS 10000
#define PLAYER_UI_VOLUME_STEP 5
#define PLAYER_UI_STICK_THRESHOLD 16000

void player_ui_reset(PlayerUiState *state);
void player_ui_clear(PlayerUiState *state);
void player_ui_sync(PlayerUiState *state, const PlayerSnapshot *snapshot);
bool player_ui_toggle_pause(PlayerUiState *state, const PlayerSnapshot *snapshot);
bool player_ui_seek(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta_ms);
bool player_ui_change_volume(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta);
int player_ui_show_controls(PlayerUiState *state, const PlayerSnapshot *snapshot);
