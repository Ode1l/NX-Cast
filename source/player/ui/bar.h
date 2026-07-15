#pragma once

#include <stdbool.h>

#include "player/player.h"
#include "player/ui/overlay.h"

int player_ui_bar_show(const PlayerSnapshot *snapshot, const char *headline, int duration_ms);
int player_ui_bar_show_help(const PlayerSnapshot *snapshot, bool paused);
void player_ui_bar_build(const PlayerSnapshot *snapshot, const char *headline, PlayerUiOverlayBar *out);
