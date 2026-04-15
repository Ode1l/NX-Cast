#pragma once

#include <stdbool.h>

#include "player/ui/ui.h"

int player_ui_controls_show(const PlayerSnapshot *snapshot, const char *headline, int duration_ms);
int player_ui_controls_show_help(const PlayerSnapshot *snapshot, bool paused);
bool player_ui_controls_toggle_pause(PlayerUiState *state, const PlayerSnapshot *snapshot);
bool player_ui_controls_seek(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta_ms);
bool player_ui_controls_change_volume(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta);
