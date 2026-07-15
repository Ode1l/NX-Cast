#pragma once

#include <stdbool.h>

typedef struct
{
    int pad_x;
    int bottom_y;
    int bottom_height;
    int progress_x;
    int progress_y;
    int progress_width;
    int progress_height;
    int progress_hit_y;
    int progress_hit_height;
} PlayerUiLayout;

bool player_ui_layout_compute(int display_width, int display_height, PlayerUiLayout *out);
bool player_ui_layout_progress_hit_test(const PlayerUiLayout *layout, int x, int y);
int player_ui_layout_progress_target_ms(const PlayerUiLayout *layout, int x, int duration_ms);
