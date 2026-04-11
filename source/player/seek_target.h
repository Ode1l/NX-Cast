#pragma once

#include <stdbool.h>

bool player_seek_target_parse_ms(const char *target, int *out_ms);
char *player_seek_target_format_hhmmss_alloc(int position_ms);
