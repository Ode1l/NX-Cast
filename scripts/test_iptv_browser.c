#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "player/ui/browser.h"
#include "player/ui/channel_list.h"

static uint64_t now = 100;
static PlayerBrowserAction press(PlayerBrowser *b, unsigned key)
{
    (void)player_browser_input(b, 0, 0, now++);
    return player_browser_input(b, key, key, now++);
}
static PlayerBrowserAction tap(PlayerBrowser *b, PlayerBrowserRect r)
{
    assert(player_browser_touch(b, true, r.x + r.w / 2, r.y + r.h / 2, now++).kind ==
           PLAYER_BROWSER_ACTION_NONE);
    return player_browser_touch(b, false, 0, 0, now++);
}
static PlayerBrowser fresh(bool playback)
{
    PlayerBrowser b;
    player_browser_init(&b);
    player_browser_open(&b, playback);
    player_browser_sync(&b, 200, 0, 67, 32);
    return b;
}
static void test_navigation(void)
{
    PlayerBrowser b = fresh(false);
    assert(b.view.row_count == 8);
    for (int i = 0; i < 8; i++)
        press(&b, PLAYER_BROWSER_DOWN);
    assert(b.view.selected_index == 8 && b.view.first_index == 1);
    assert(b.view.scroll_offset == 56);
    PlayerBrowserRect r = player_browser_row_rect(&b.view, 8);
    assert(r.y == 558);
    player_browser_sync(&b, 0, 100, 0, 0);
    assert(b.view.selected_index == 0 && b.view.row_count == 0 && b.view.scroll_offset == 0);
    assert(press(&b, PLAYER_BROWSER_ACCEPT).kind == PLAYER_BROWSER_ACTION_NONE);
    b = fresh(true);
    assert(b.view.row_count == 9 && player_browser_panel_rect(&b.view).w == 564);
    press(&b, PLAYER_BROWSER_EXPAND);
    assert(b.view.page == PLAYER_BROWSER_LIBRARY && b.view.playback_active);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_DRAWER);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_CLOSED);
    assert(press(&b, PLAYER_BROWSER_ACCEPT).kind == PLAYER_BROWSER_ACTION_NONE);
}
static void test_acceleration(void)
{
    PlayerBrowser b = fresh(false);
    player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, 1000);
    assert(b.view.selected_index == 1);
    player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, 1349);
    assert(b.view.selected_index == 1);
    player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, 1350);
    assert(b.view.selected_index == 2);
    for (uint64_t t = 1400; t < 2800; t += 10)
        player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, t);
    int before = b.view.selected_index;
    for (uint64_t t = 2800; t < 3200; t += 10)
        player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, t);
    assert(b.view.selected_index - before >= 8);
    press(&b, PLAYER_BROWSER_UP);
    assert(b.held_direction == PLAYER_BROWSER_UP);
}
static void test_touch(void)
{
    PlayerBrowser b = fresh(false);
    PlayerBrowserRect r = player_browser_row_rect(&b.view, 3);
    assert(tap(&b, r).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(b.view.selected_index == 3);
    assert(tap(&b, r).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(tap(&b, player_browser_action_rect(&b.view, 0)).kind == PLAYER_BROWSER_ACTION_PLAY);
    player_browser_tick(&b, 1000);
    player_browser_touch(&b, true, 100, 500, 1000);
    player_browser_touch(&b, true, 100, 350, 1040);
    player_browser_touch(&b, true, 100, 200, 1080);
    assert(player_browser_touch(&b, false, 0, 0, 1090).kind == PLAYER_BROWSER_ACTION_NONE);
    float offset = b.view.scroll_offset;
    player_browser_tick(&b, 1100);
    assert(b.view.scroll_offset > offset);
    assert(b.view.selected_index == 3);
    /* A gesture that returns to its origin is still a drag, not a tap. */
    player_browser_touch(&b, true, 100, 300, 1200);
    player_browser_touch(&b, true, 100, 330, 1230);
    player_browser_touch(&b, true, 100, 300, 1260);
    assert(player_browser_touch(&b, false, 0, 0, 1270).kind == PLAYER_BROWSER_ACTION_NONE);
    PlayerBrowserRect track = player_browser_scrollbar_rect(&b.view, false);
    player_browser_touch(&b, true, track.x + 2, track.y + track.h - 1, 1300);
    assert(player_browser_touch(&b, false, 0, 0, 1310).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(b.velocity == 0);
    assert(b.view.first_index == 192);
    player_browser_sync(&b, 2, 0, 67, 32);
    assert(b.view.first_index == 0 && b.view.row_count == 2);
}
static PlayerBrowser flick(bool playback, unsigned interval)
{
    PlayerBrowser b = fresh(playback);
    player_browser_touch(&b, true, 100, 500, 1000);
    for (unsigned i = 1; i <= 5; i++)
        player_browser_touch(&b, true, 100, 500 - i * 40, 1000 + i * interval);
    assert(fabsf(b.view.scroll_offset - 200) < 1); /* Always follow the finger. */
    assert(player_browser_touch(&b, false, 0, 0, 1001 + 5 * interval).kind ==
           PLAYER_BROWSER_ACTION_NONE);
    return b;
}
static void test_flick(void)
{
    for (int playback = 0; playback <= 1; playback++)
    {
        PlayerBrowser slow = flick(playback, 160), fast = flick(playback, 16);
        assert(slow.velocity > 200 && slow.velocity < 300);
        assert(fast.velocity > slow.velocity * 10 && fast.velocity <= 9000);
        PlayerBrowser fine = fast;
        fast.last_tick = fine.last_tick = 2000;
        for (unsigned t = 2040; t <= 3000; t += 40)
            player_browser_tick(&fast, t);
        for (unsigned t = 2010; t <= 3000; t += 10)
            player_browser_tick(&fine, t);
        assert(fabsf(fast.view.scroll_offset - fine.view.scroll_offset) < 1);
        assert(fast.view.scroll_offset > 1000);
        player_browser_touch(&fast, true, 100, 400, 3010);
        assert(fast.velocity == 0); /* Touch immediately brakes inertia. */
        player_browser_touch(&fast, true, 100, 300, 3026);
        player_browser_touch(&fast, true, 100, 340, 3042);
        assert(fast.velocity < 0); /* Reversal must not coast the old way. */
        for (unsigned t = 3058; t <= 3378; t += 16)
            player_browser_touch(&fast, true, 100, 340, t);
        player_browser_touch(&fast, false, 0, 0, 3390);
        assert(fabsf(fast.velocity) < 2); /* Hold still before releasing. */
        fast.view.scroll_offset = 1;
        fast.velocity = -9000;
        player_browser_tick(&fast, 3400);
        assert(fast.view.scroll_offset == 0 && fast.velocity == 0);
        fast.view.scroll_offset = 1000000;
        fast.velocity = 9000;
        player_browser_tick(&fast, 3440);
        assert(fast.view.scroll_offset < 1000000 && fast.velocity == 0);
    }
}
static void test_modals_and_sources(void)
{
    PlayerBrowser b = fresh(true);
    tap(&b, player_browser_toolbar_rect(&b.view, 0));
    assert(b.view.modal == PLAYER_BROWSER_MODAL_CATEGORIES && b.view.modal_count == 67);
    for (int i = 0; i < 8; i++)
        press(&b, PLAYER_BROWSER_DOWN);
    assert(b.view.modal_cursor == 24 && b.view.modal_first_index > 0);
    PlayerBrowserAction a = press(&b, PLAYER_BROWSER_ACCEPT);
    assert(a.kind == PLAYER_BROWSER_ACTION_FILTER && a.index == 24);
    assert(b.view.modal == PLAYER_BROWSER_MODAL_NONE && b.view.page == PLAYER_BROWSER_DRAWER);
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    a = tap(&b, player_browser_modal_item_rect(&b.view, 0));
    assert(a.kind == PLAYER_BROWSER_ACTION_SOURCE_FILTER && a.index == -1);
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    press(&b, PLAYER_BROWSER_DOWN);
    press(&b, PLAYER_BROWSER_DOWN);
    a = press(&b, PLAYER_BROWSER_ACCEPT);
    assert(a.kind == PLAYER_BROWSER_ACTION_SOURCE_FILTER && a.index == 0);
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    for (int i = 0; i < 33; i++)
        press(&b, PLAYER_BROWSER_DOWN);
    assert(b.view.modal_cursor == 33 && b.view.modal_first_index == 30);
    a = press(&b, PLAYER_BROWSER_ACCEPT);
    assert(a.kind == PLAYER_BROWSER_ACTION_SOURCE_FILTER && a.index == 31);
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    press(&b, PLAYER_BROWSER_DOWN);
    press(&b, PLAYER_BROWSER_ACCEPT);
    assert(b.view.page == PLAYER_BROWSER_SOURCES && b.view.playback_active);
    player_browser_sync(&b, 32, 7, 67, 32);
    assert(b.view.selected_index == 7);
    press(&b, PLAYER_BROWSER_ACCEPT);
    assert(b.view.focus == PLAYER_BROWSER_FOCUS_ACTIONS);
    const PlayerBrowserActionKind expected[] = {
        PLAYER_BROWSER_ACTION_ADD_URL, PLAYER_BROWSER_ACTION_SCAN_SD, PLAYER_BROWSER_ACTION_REFRESH,
        PLAYER_BROWSER_ACTION_EPG};
    for (int i = 0; i < 4; i++)
    {
        assert(press(&b, PLAYER_BROWSER_ACCEPT).kind == expected[i]);
        press(&b, PLAYER_BROWSER_RIGHT);
    }
    press(&b, PLAYER_BROWSER_ACCEPT);
    assert(b.view.modal == PLAYER_BROWSER_MODAL_DELETE);
    assert(press(&b, PLAYER_BROWSER_ACCEPT).kind == PLAYER_BROWSER_ACTION_NONE);
    press(&b, PLAYER_BROWSER_ACCEPT);
    press(&b, PLAYER_BROWSER_DOWN);
    assert(press(&b, PLAYER_BROWSER_ACCEPT).kind == PLAYER_BROWSER_ACTION_DELETE);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_DRAWER);
    tap(&b, player_browser_toolbar_rect(&b.view, 0));
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_DRAWER && b.view.modal == PLAYER_BROWSER_MODAL_NONE);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_CLOSED);
}
static void test_modal_touch_and_empty_sources(void)
{
    PlayerBrowser b = fresh(false);
    assert(tap(&b, player_browser_toolbar_rect(&b.view, 1)).index == 1);
    assert(tap(&b, player_browser_toolbar_rect(&b.view, 2)).index == 2);
    assert(tap(&b, player_browser_toolbar_rect(&b.view, 3)).kind == PLAYER_BROWSER_ACTION_SEARCH);
    tap(&b, player_browser_toolbar_rect(&b.view, 0));
    player_browser_touch(&b, true, 220, 400, 5000);
    player_browser_touch(&b, true, 220, 200, 5040);
    assert(player_browser_touch(&b, false, 0, 0, 5050).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(b.view.modal == PLAYER_BROWSER_MODAL_CATEGORIES && b.view.modal_first_index > 0);
    press(&b, PLAYER_BROWSER_BACK);
    player_browser_sync(&b, 0, 0, 3, 0);
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    assert(b.view.modal_count == 2);
    press(&b, PLAYER_BROWSER_DOWN);
    press(&b, PLAYER_BROWSER_ACCEPT);
    assert(b.view.page == PLAYER_BROWSER_SOURCES);
    player_browser_sync(&b, 0, 0, 3, 0);
    assert(tap(&b, player_browser_action_rect(&b.view, 2)).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(tap(&b, player_browser_action_rect(&b.view, 3)).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(tap(&b, player_browser_action_rect(&b.view, 4)).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(tap(&b, player_browser_action_rect(&b.view, 0)).kind == PLAYER_BROWSER_ACTION_ADD_URL);
    assert(tap(&b, player_browser_action_rect(&b.view, 1)).kind == PLAYER_BROWSER_ACTION_SCAN_SD);
    /* Reopening Manage while already there must not overwrite its parent. */
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    press(&b, PLAYER_BROWSER_DOWN);
    press(&b, PLAYER_BROWSER_ACCEPT);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_LIBRARY);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.page == PLAYER_BROWSER_CLOSED);
}
static void test_transition_capture(void)
{
    PlayerBrowser b = fresh(false);
    player_browser_sync(&b, 200, 125, 67, 32);
    float saved = b.view.scroll_offset;
    tap(&b, player_browser_toolbar_rect(&b.view, 4));
    /* Holding Down while a modal opens cannot move its new cursor. */
    for (uint64_t t = 6000; t < 8000; t += 20)
        player_browser_input(&b, 0, PLAYER_BROWSER_DOWN, t);
    assert(b.view.modal_cursor == 0);
    press(&b, PLAYER_BROWSER_DOWN);
    press(&b, PLAYER_BROWSER_ACCEPT);
    player_browser_sync(&b, 32, 7, 67, 32);
    press(&b, PLAYER_BROWSER_BACK);
    assert(b.view.selected_index == 125);
    player_browser_sync(&b, 200, 125, 67, 32);
    assert(b.view.selected_index == 125 && b.view.scroll_offset == saved);
    player_browser_sync(&b, 0, 0, 3, 0);
    press(&b, PLAYER_BROWSER_LEFT);
    assert(b.view.focus == PLAYER_BROWSER_FOCUS_TOOLBAR);
    for (int i = 0; i < 5; i++)
    {
        if (b.view.toolbar_focus == PLAYER_BROWSER_SOURCE_FILTER)
            break;
        press(&b, PLAYER_BROWSER_RIGHT);
    }
    press(&b, PLAYER_BROWSER_ACCEPT);
    assert(b.view.modal == PLAYER_BROWSER_MODAL_SOURCES);
}
int main(void)
{
    PlayerBrowser drawer = fresh(true);
    PlayerBrowserRect outside = {800, 300, 40, 40};
    assert(tap(&drawer, outside).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(drawer.view.page == PLAYER_BROWSER_CLOSED);
    drawer = fresh(true);
    player_browser_touch(&drawer, true, 820, 320, now++);
    player_browser_touch(&drawer, true, 820, 360, now++);
    player_browser_touch(&drawer, false, 0, 0, now++);
    assert(drawer.view.page == PLAYER_BROWSER_DRAWER);
    drawer.view.modal = PLAYER_BROWSER_MODAL_SOURCES;
    assert(tap(&drawer, (PlayerBrowserRect){1160, 300, 40, 40}).kind == PLAYER_BROWSER_ACTION_NONE);
    assert(drawer.view.modal == PLAYER_BROWSER_MODAL_NONE);
    assert(drawer.view.page == PLAYER_BROWSER_DRAWER);
    assert(player_iptv_video_menu_available(true, 0));
    assert(player_iptv_video_menu_available(true, 10000));
    assert(!player_iptv_video_menu_available(false, 10000));
    test_navigation();
    test_acceleration();
    test_touch();
    test_flick();
    test_modals_and_sources();
    test_modal_touch_and_empty_sources();
    test_transition_capture();
    puts("IPTV browser tests passed");
    return 0;
}
