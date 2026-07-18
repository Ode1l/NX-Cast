#pragma once

#include <stdbool.h>

#define PLAYER_IPTV_VISIBLE_ROWS 7
#define PLAYER_IPTV_LIST_X 72
#define PLAYER_IPTV_LIST_WIDTH 690
#define PLAYER_IPTV_LIST_TOP 166
#define PLAYER_IPTV_ROW_HEIGHT 62
#define PLAYER_IPTV_PAGE_BUTTON_TOP 128
#define PLAYER_IPTV_PAGE_BUTTON_BOTTOM 160
#define PLAYER_IPTV_PAGE_PREV_LEFT 632
#define PLAYER_IPTV_PAGE_PREV_RIGHT 688
#define PLAYER_IPTV_PAGE_NEXT_LEFT 696
#define PLAYER_IPTV_PAGE_NEXT_RIGHT 752
#define PLAYER_IPTV_CHANNEL_TAB_LEFT 314
#define PLAYER_IPTV_CHANNEL_TAB_RIGHT 430
#define PLAYER_IPTV_SOURCE_TAB_LEFT 440
#define PLAYER_IPTV_SOURCE_TAB_RIGHT 548
#define PLAYER_IPTV_TAB_TOP 62
#define PLAYER_IPTV_TAB_BOTTOM 102
#define PLAYER_IPTV_CLOSE_LEFT 1152
#define PLAYER_IPTV_CLOSE_RIGHT 1210
#define PLAYER_IPTV_CLOSE_TOP 58
#define PLAYER_IPTV_CLOSE_BOTTOM 108
#define PLAYER_IPTV_ACTION_LEFT 806
#define PLAYER_IPTV_ACTION_RIGHT 1208
#define PLAYER_IPTV_ACTION_TOP 558
#define PLAYER_IPTV_ACTION_BOTTOM 606
#define PLAYER_IPTV_DRAWER_CLOSE_LEFT 704
#define PLAYER_IPTV_DRAWER_CLOSE_RIGHT 762
#define PLAYER_IPTV_DRAWER_CLOSE_TOP 50
#define PLAYER_IPTV_DRAWER_CLOSE_BOTTOM 100
#define PLAYER_IPTV_DRAWER_ACTION_LEFT 548
#define PLAYER_IPTV_DRAWER_ACTION_RIGHT 752
#define PLAYER_IPTV_DRAWER_ACTION_TOP 616
#define PLAYER_IPTV_DRAWER_ACTION_BOTTOM 662
#define PLAYER_IPTV_SWIPE_MIN_PX 54

static inline bool player_iptv_point_in_rect(int x,
                                             int y,
                                             int left,
                                             int top,
                                             int right,
                                             int bottom)
{
    return x >= left && x <= right && y >= top && y <= bottom;
}

static inline int player_iptv_page_start(int selected_index)
{
    if (selected_index < 0)
        return 0;
    return (selected_index / PLAYER_IPTV_VISIBLE_ROWS) * PLAYER_IPTV_VISIBLE_ROWS;
}

static inline bool player_iptv_close_hit(int x, int y, bool sources_open)
{
    return sources_open
               ? player_iptv_point_in_rect(x,
                                           y,
                                           PLAYER_IPTV_CLOSE_LEFT,
                                           PLAYER_IPTV_CLOSE_TOP,
                                           PLAYER_IPTV_CLOSE_RIGHT,
                                           PLAYER_IPTV_CLOSE_BOTTOM)
               : player_iptv_point_in_rect(x,
                                           y,
                                           PLAYER_IPTV_DRAWER_CLOSE_LEFT,
                                           PLAYER_IPTV_DRAWER_CLOSE_TOP,
                                           PLAYER_IPTV_DRAWER_CLOSE_RIGHT,
                                           PLAYER_IPTV_DRAWER_CLOSE_BOTTOM);
}

static inline bool player_iptv_action_hit(int x, int y, bool sources_open)
{
    return sources_open
               ? player_iptv_point_in_rect(x,
                                           y,
                                           PLAYER_IPTV_ACTION_LEFT,
                                           PLAYER_IPTV_ACTION_TOP,
                                           PLAYER_IPTV_ACTION_RIGHT,
                                           PLAYER_IPTV_ACTION_BOTTOM)
               : player_iptv_point_in_rect(x,
                                           y,
                                           PLAYER_IPTV_DRAWER_ACTION_LEFT,
                                           PLAYER_IPTV_DRAWER_ACTION_TOP,
                                           PLAYER_IPTV_DRAWER_ACTION_RIGHT,
                                           PLAYER_IPTV_DRAWER_ACTION_BOTTOM);
}

static inline int player_iptv_touch_row_index(int x,
                                               int y,
                                               int selected_index,
                                               int item_count)
{
    int row;
    int index;

    if (item_count <= 0 ||
        !player_iptv_point_in_rect(x,
                                   y,
                                   PLAYER_IPTV_LIST_X,
                                   PLAYER_IPTV_LIST_TOP,
                                   PLAYER_IPTV_LIST_X + PLAYER_IPTV_LIST_WIDTH,
                                   PLAYER_IPTV_LIST_TOP + PLAYER_IPTV_ROW_HEIGHT * PLAYER_IPTV_VISIBLE_ROWS))
        return -1;

    row = (y - PLAYER_IPTV_LIST_TOP) / PLAYER_IPTV_ROW_HEIGHT;
    if (row >= PLAYER_IPTV_VISIBLE_ROWS)
        row = PLAYER_IPTV_VISIBLE_ROWS - 1;
    index = player_iptv_page_start(selected_index) + row;
    return index < item_count ? index : -1;
}

static inline int player_iptv_swipe_page_delta(int start_x,
                                                int start_y,
                                                int end_x,
                                                int end_y)
{
    int dx;
    int dy;

    if (!player_iptv_point_in_rect(start_x,
                                   start_y,
                                   PLAYER_IPTV_LIST_X,
                                   PLAYER_IPTV_LIST_TOP,
                                   PLAYER_IPTV_LIST_X + PLAYER_IPTV_LIST_WIDTH,
                                   PLAYER_IPTV_LIST_TOP + PLAYER_IPTV_ROW_HEIGHT * PLAYER_IPTV_VISIBLE_ROWS))
        return 0;

    dx = end_x - start_x;
    dy = end_y - start_y;
    if (dx * dx > dy * dy)
    {
        if (dx <= -PLAYER_IPTV_SWIPE_MIN_PX)
            return 1;
        if (dx >= PLAYER_IPTV_SWIPE_MIN_PX)
            return -1;
        return 0;
    }
    if (dy <= -PLAYER_IPTV_SWIPE_MIN_PX)
        return 1;
    if (dy >= PLAYER_IPTV_SWIPE_MIN_PX)
        return -1;
    return 0;
}
