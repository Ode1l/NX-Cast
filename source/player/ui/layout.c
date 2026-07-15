#include "player/ui/layout.h"

#define PLAYER_UI_PROGRESS_HIT_SLOP 52

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

bool player_ui_layout_compute(int display_width, int display_height, PlayerUiLayout *out)
{
    int progress_hit_y;
    int progress_hit_bottom;

    if (!out || display_width <= 0 || display_height <= 0)
        return false;

    out->pad_x = clamp_int(display_width / 18, 48, 88);
    out->bottom_height = clamp_int(display_height / 6, 112, 154);
    out->bottom_y = display_height - out->bottom_height;
    out->progress_height = clamp_int(display_height / 100, 6, 10);
    out->progress_y = out->bottom_y + clamp_int(out->bottom_height / 5, 20, 30);
    out->progress_x = out->pad_x;
    out->progress_width = display_width - out->pad_x * 2;

    progress_hit_y = out->progress_y - PLAYER_UI_PROGRESS_HIT_SLOP;
    progress_hit_bottom = out->progress_y + out->progress_height + PLAYER_UI_PROGRESS_HIT_SLOP;
    out->progress_hit_y = clamp_int(progress_hit_y, 0, display_height);
    out->progress_hit_height = clamp_int(progress_hit_bottom, out->progress_hit_y, display_height) - out->progress_hit_y;
    return out->progress_width > 0 && out->progress_height > 0 && out->progress_hit_height > 0;
}

bool player_ui_layout_progress_hit_test(const PlayerUiLayout *layout, int x, int y)
{
    if (!layout)
        return false;

    return x >= layout->progress_x &&
           x <= layout->progress_x + layout->progress_width &&
           y >= layout->progress_hit_y &&
           y <= layout->progress_hit_y + layout->progress_hit_height;
}

int player_ui_layout_progress_target_ms(const PlayerUiLayout *layout, int x, int duration_ms)
{
    int clamped_x;
    long long offset;

    if (!layout || layout->progress_width <= 0 || duration_ms <= 0)
        return 0;

    clamped_x = clamp_int(x, layout->progress_x, layout->progress_x + layout->progress_width);
    offset = (long long)(clamped_x - layout->progress_x);
    return clamp_int((int)((offset * (long long)duration_ms + layout->progress_width / 2) /
                           layout->progress_width),
                     0,
                     duration_ms);
}
