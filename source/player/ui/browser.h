#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* All geometry is in the Switch touch/display coordinate space: 1280 x 720. */
    typedef struct
    {
        float x, y, w, h;
    } PlayerBrowserRect;
    typedef enum
    {
        PLAYER_BROWSER_CLOSED,
        PLAYER_BROWSER_LIBRARY,
        PLAYER_BROWSER_DRAWER,
        PLAYER_BROWSER_SOURCES
    } PlayerBrowserPage;
    typedef enum
    {
        PLAYER_BROWSER_MODAL_NONE,
        PLAYER_BROWSER_MODAL_CATEGORIES,
        PLAYER_BROWSER_MODAL_SOURCES,
        PLAYER_BROWSER_MODAL_DELETE
    } PlayerBrowserModal;
    typedef enum
    {
        PLAYER_BROWSER_FOCUS_ROWS,
        PLAYER_BROWSER_FOCUS_TOOLBAR,
        PLAYER_BROWSER_FOCUS_ACTIONS
    } PlayerBrowserFocus;
    typedef enum
    {
        PLAYER_BROWSER_CATEGORIES,
        PLAYER_BROWSER_FAVORITES,
        PLAYER_BROWSER_RECENT,
        PLAYER_BROWSER_SEARCH,
        PLAYER_BROWSER_SOURCE_FILTER,
        PLAYER_BROWSER_TOOLBAR_COUNT
    } PlayerBrowserToolbar;
    typedef enum
    {
        PLAYER_BROWSER_ACTION_NONE,
        PLAYER_BROWSER_ACTION_PLAY,
        PLAYER_BROWSER_ACTION_FILTER,
        PLAYER_BROWSER_ACTION_SOURCE_FILTER,
        PLAYER_BROWSER_ACTION_SEARCH,
        PLAYER_BROWSER_ACTION_FAVORITE,
        PLAYER_BROWSER_ACTION_ADD_URL,
        PLAYER_BROWSER_ACTION_SCAN_SD,
        PLAYER_BROWSER_ACTION_REFRESH,
        PLAYER_BROWSER_ACTION_EPG,
        PLAYER_BROWSER_ACTION_DELETE
    } PlayerBrowserActionKind;
    typedef struct
    {
        PlayerBrowserActionKind kind;
        int index;
    } PlayerBrowserAction;
    typedef struct
    {
        PlayerBrowserPage page;
        PlayerBrowserModal modal;
        PlayerBrowserFocus focus;
        bool playback_active;
        int selected_index, item_count, first_index, row_count;
        int toolbar_focus, action_focus;
        int modal_cursor, modal_count, modal_first_index;
        float scroll_offset, modal_scroll_offset;
    } PlayerBrowserView;
    typedef struct
    {
        PlayerBrowserView view;
        PlayerBrowserPage return_page;
    int return_selected_index;
    int return_item_count;
        float return_scroll_offset;
        float velocity, touch_start_x, touch_start_y, touch_last_y;
        bool touching, dragging, scrollbar_drag, suppress_held;
        int held_direction;
        int filter_count, source_count;
        uint64_t held_since, next_repeat, last_tick, touch_time;
    } PlayerBrowser;

    enum
    {
        PLAYER_BROWSER_UP = 1,
        PLAYER_BROWSER_DOWN = 2,
        PLAYER_BROWSER_LEFT = 4,
        PLAYER_BROWSER_RIGHT = 8,
        PLAYER_BROWSER_ACCEPT = 16,
        PLAYER_BROWSER_BACK = 32,
        PLAYER_BROWSER_EXPAND = 64,
        PLAYER_BROWSER_FAVORITE_KEY = 128
    };

    PlayerBrowserRect player_browser_panel_rect(const PlayerBrowserView *v);
    PlayerBrowserRect player_browser_list_rect(const PlayerBrowserView *v);
    /* Index is absolute in iptv_get_channel's filtered visible list, not viewport-relative. */
    PlayerBrowserRect player_browser_row_rect(const PlayerBrowserView *v, int absolute_index);
    PlayerBrowserRect player_browser_toolbar_rect(const PlayerBrowserView *v, int index);
    PlayerBrowserRect player_browser_action_rect(const PlayerBrowserView *v, int index);
    PlayerBrowserRect player_browser_close_rect(const PlayerBrowserView *v);
    PlayerBrowserRect player_browser_expand_rect(const PlayerBrowserView *v);
    PlayerBrowserRect player_browser_modal_rect(const PlayerBrowserView *v);
/* Sources modal: 0 All, 1 Manage, 2+ source index + 2. Categories use filter indices. */
PlayerBrowserRect player_browser_modal_item_rect(const PlayerBrowserView *v, int index);
    PlayerBrowserRect player_browser_scrollbar_rect(const PlayerBrowserView *v, bool thumb);
    int player_browser_action_count(const PlayerBrowserView *v);
    void player_browser_init(PlayerBrowser *b);
    void player_browser_open(PlayerBrowser *b, bool playback_active);
    void player_browser_sync(PlayerBrowser *b, int count, int selected, int filter_count,
                             int source_count);
    void player_browser_tick(PlayerBrowser *b, uint64_t now_ms);
    PlayerBrowserAction player_browser_input(PlayerBrowser *b, unsigned pressed, unsigned held,
                                             uint64_t now_ms);
    PlayerBrowserAction player_browser_touch(PlayerBrowser *b, bool down, float x, float y,
                                             uint64_t now_ms);
/* Touch rows select only. Play is always the explicit primary button or A/SR. */
#ifdef __cplusplus
}
#endif
