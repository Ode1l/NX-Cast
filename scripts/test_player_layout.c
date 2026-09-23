#include <assert.h>
#include <stdio.h>
#include "player/ui/layout.h"

int main(void)
{
    const int widths[] = {1280, 1920};
    const int heights[] = {720, 1080};
    PlayerUiLayout layout;
    assert(!player_ui_layout_compute(0, 720, &layout));
    assert(!player_ui_layout_compute(1280, 720, NULL));
    for (int i = 0; i < 2; ++i)
    {
        assert(player_ui_layout_compute(widths[i], heights[i], &layout));
        assert(layout.progress_height <= 6);
        assert(layout.progress_hit_height >= 96);
        assert(layout.title_y + 24 < layout.progress_y);
        assert(layout.progress_y + layout.progress_height < layout.info_y);
        assert(layout.info_y + 18 < layout.hints_y - 12);
        assert(layout.hints_y + 12 < heights[i]);
        assert(player_ui_layout_progress_hit_test(&layout, layout.progress_x, layout.progress_y - 30));
        assert(!player_ui_layout_progress_hit_test(&layout, layout.progress_x - 1, layout.progress_y));
        assert(player_ui_layout_progress_target_ms(&layout, layout.progress_x, 100000) == 0);
        assert(player_ui_layout_progress_target_ms(&layout, widths[i], 100000) == 100000);
        assert(player_ui_layout_progress_target_ms(&layout, widths[i] / 2, 100000) == 50000);
    }
    puts("Player visual spacing and touch geometry passed");
    return 0;
}
