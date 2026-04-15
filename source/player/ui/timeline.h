#pragma once

#include <stddef.h>

#include "player/player.h"

void player_ui_timeline_describe(const PlayerSnapshot *snapshot, char *out, size_t out_size);
