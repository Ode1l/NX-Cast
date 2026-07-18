#include <assert.h>
#include <stdio.h>

#include "player/ui/channel_list.h"

int main(void)
{
    assert(player_iptv_page_start(0) == 0);
    assert(player_iptv_page_start(6) == 0);
    assert(player_iptv_page_start(7) == 7);
    assert(player_iptv_page_start(31) == 28);

    assert(player_iptv_touch_row_index(100, PLAYER_IPTV_LIST_TOP + 4, 0, 20) == 0);
    assert(player_iptv_touch_row_index(100, PLAYER_IPTV_LIST_TOP + PLAYER_IPTV_ROW_HEIGHT * 6 + 4, 7, 20) == 13);
    assert(player_iptv_touch_row_index(100, PLAYER_IPTV_LIST_TOP + PLAYER_IPTV_ROW_HEIGHT * 6 + 4, 14, 18) == -1);
    assert(player_iptv_touch_row_index(10, PLAYER_IPTV_LIST_TOP, 0, 20) == -1);

    assert(player_iptv_close_hit(730, 72, false));
    assert(!player_iptv_close_hit(730, 72, true));
    assert(player_iptv_close_hit(1180, 72, true));
    assert(player_iptv_action_hit(650, 640, false));
    assert(!player_iptv_action_hit(650, 640, true));
    assert(player_iptv_action_hit(1000, 580, true));

    assert(player_iptv_swipe_page_delta(200, 300, 200, 220) == 1);
    assert(player_iptv_swipe_page_delta(200, 300, 200, 380) == -1);
    assert(player_iptv_swipe_page_delta(300, 300, 220, 300) == 1);
    assert(player_iptv_swipe_page_delta(300, 300, 380, 300) == -1);
    assert(player_iptv_swipe_page_delta(10, 300, 10, 200) == 0);

    puts("IPTV channel-list navigation tests passed");
    return 0;
}
