#pragma once

#include <stddef.h>

#include "player/player.h"

int player_ui_timeline_progress_permille(const PlayerSnapshot *snapshot);
void player_ui_timeline_format_time(int position_ms, char *out, size_t out_size);
void player_ui_timeline_describe(const PlayerSnapshot *snapshot, char *out, size_t out_size);
