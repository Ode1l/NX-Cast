#include "browser.h"
#include <math.h>
#include <string.h>

static float clamp(float n, float low, float high)
{
    return n < low ? low : n > high ? high : n;
}

static bool hit(PlayerBrowserRect r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static PlayerBrowserAction action(PlayerBrowserActionKind kind, int index)
{
    return (PlayerBrowserAction){kind, index};
}

static PlayerBrowserAction none(void)
{
    return action(PLAYER_BROWSER_ACTION_NONE, 0);
}

static int columns(const PlayerBrowserView *v)
{
    return v->modal == PLAYER_BROWSER_MODAL_CATEGORIES ? 3 : 1;
}

static float row_height(const PlayerBrowserView *v)
{
    return v->page == PLAYER_BROWSER_DRAWER ? 50 : 56;
}

PlayerBrowserRect player_browser_panel_rect(const PlayerBrowserView *v)
{
    return (PlayerBrowserRect){0, 0, v->page == PLAYER_BROWSER_DRAWER ? 564 : 1280, 720};
}

PlayerBrowserRect player_browser_list_rect(const PlayerBrowserView *v)
{
    return (PlayerBrowserRect){24, 166, player_browser_panel_rect(v).w - 60,
                               v->page == PLAYER_BROWSER_DRAWER ? 450 : 448};
}

PlayerBrowserRect player_browser_row_rect(const PlayerBrowserView *v, int i)
{
    PlayerBrowserRect r = player_browser_list_rect(v);
    r.y += i * row_height(v) - v->scroll_offset;
    r.h = row_height(v);
    return r;
}

PlayerBrowserRect player_browser_toolbar_rect(const PlayerBrowserView *v, int i)
{
    float w = (player_browser_panel_rect(v).w - 48) / 5;
    return (PlayerBrowserRect){24 + i * w, 110, w - 4, 40};
}

int player_browser_action_count(const PlayerBrowserView *v)
{
    return v->page == PLAYER_BROWSER_SOURCES ? 5 : 2;
}

PlayerBrowserRect player_browser_action_rect(const PlayerBrowserView *v, int i)
{
    float w = (player_browser_panel_rect(v).w - 48) / player_browser_action_count(v);
    return (PlayerBrowserRect){24 + i * w, 640, w - 6, 48};
}

PlayerBrowserRect player_browser_close_rect(const PlayerBrowserView *v)
{
    return (PlayerBrowserRect){player_browser_panel_rect(v).w - 64, 28, 40, 48};
}

PlayerBrowserRect player_browser_expand_rect(const PlayerBrowserView *v)
{
    return (PlayerBrowserRect){player_browser_panel_rect(v).w - 200, 28, 124, 48};
}

PlayerBrowserRect player_browser_modal_rect(const PlayerBrowserView *v)
{
    (void)v;
    return (PlayerBrowserRect){160, 100, 960, 520};
}

PlayerBrowserRect player_browser_modal_item_rect(const PlayerBrowserView *v, int i)
{
    int cols = columns(v);
    float w = 896.0f / cols;
    return (PlayerBrowserRect){192 + (i % cols) * w, 180 + (i / cols) * 84 - v->modal_scroll_offset,
                               w - 12, 76};
}

static int modal_rows(const PlayerBrowserView *v)
{
    return (v->modal_count + columns(v) - 1) / columns(v);
}

static float maximum(const PlayerBrowserView *v)
{
    if (v->modal)
        return fmaxf(0, modal_rows(v) * 84 - 336);
    return fmaxf(0, v->item_count * row_height(v) - player_browser_list_rect(v).h);
}

PlayerBrowserRect player_browser_scrollbar_rect(const PlayerBrowserView *v, bool thumb)
{
    PlayerBrowserRect r =
        v->modal ? (PlayerBrowserRect){1092, 180, 12, 336} : player_browser_list_rect(v);
    if (!v->modal)
    {
        r.x += r.w + 8;
        r.w = 12;
    }
    if (thumb)
    {
        float extent = maximum(v) + r.h;
        float h = fmaxf(30, r.h * r.h / extent);
        float off = v->modal ? v->modal_scroll_offset : v->scroll_offset;
        r.y += maximum(v) > 0 ? (r.h - h) * off / maximum(v) : 0;
        r.h = h;
    }
    return r;
}

static void window(PlayerBrowser *b)
{
    PlayerBrowserView *v = &b->view;
    v->selected_index = (int)clamp(v->selected_index, 0, v->item_count > 0 ? v->item_count - 1 : 0);
    v->scroll_offset =
        clamp(v->scroll_offset, 0,
              fmaxf(0, v->item_count * row_height(v) - player_browser_list_rect(v).h));
    v->first_index = (int)(v->scroll_offset / row_height(v));
    v->row_count = (int)ceilf(
        (player_browser_list_rect(v).h + fmodf(v->scroll_offset, row_height(v))) / row_height(v));
    if (v->row_count > v->item_count - v->first_index)
        v->row_count = v->item_count - v->first_index;
    v->modal_scroll_offset = clamp(v->modal_scroll_offset, 0, v->modal ? maximum(v) : 0);
    v->modal_first_index = (int)(v->modal_scroll_offset / 84) * columns(v);
}

static void reveal(PlayerBrowser *b)
{
    PlayerBrowserView *v = &b->view;
    float *off = v->modal ? &v->modal_scroll_offset : &v->scroll_offset;
    float top = v->modal ? (v->modal_cursor / columns(v)) * 84 : v->selected_index * row_height(v);
    float h = v->modal ? 84 : row_height(v),
          viewport = v->modal ? 336 : player_browser_list_rect(v).h;
    if (top < *off)
        *off = top;
    if (top + h > *off + viewport)
        *off = top + h - viewport;
    window(b);
}

void player_browser_init(PlayerBrowser *b)
{
    memset(b, 0, sizeof(*b));
}

void player_browser_open(PlayerBrowser *b, bool playback)
{
    b->view.page = playback ? PLAYER_BROWSER_DRAWER : PLAYER_BROWSER_LIBRARY;
    b->view.playback_active = playback;
    b->view.modal = PLAYER_BROWSER_MODAL_NONE;
    b->view.focus = PLAYER_BROWSER_FOCUS_ROWS;
    b->velocity = 0;
    b->touching = false;
    b->held_direction = 0;
    reveal(b);
}

void player_browser_sync(PlayerBrowser *b, int count, int selected, int filters, int sources)
{
    bool selection_changed = b->view.selected_index != selected || b->view.item_count != count;
    b->view.item_count = count > 0 ? count : 0;
    b->view.selected_index = selected;
    b->filter_count = filters;
    b->source_count = sources;
    if (b->view.modal == PLAYER_BROWSER_MODAL_CATEGORIES)
        b->view.modal_count = filters;
    if (b->view.modal == PLAYER_BROWSER_MODAL_SOURCES)
        b->view.modal_count = sources + 2;
    if (b->view.modal_count < 0)
        b->view.modal_count = 0;
    b->view.modal_cursor =
        (int)clamp(b->view.modal_cursor, 0, b->view.modal_count > 0 ? b->view.modal_count - 1 : 0);
    window(b);
    if (selection_changed && !b->touching && !b->view.modal)
        reveal(b);
}

static void modal(PlayerBrowser *b, PlayerBrowserModal m, int count)
{
    b->view.modal = m;
    b->view.modal_count = count;
    b->view.modal_cursor = 0;
    b->view.modal_scroll_offset = 0;
    b->velocity = 0;
    b->touching = false;
    b->suppress_held = true;
}

static PlayerBrowserAction toolbar(PlayerBrowser *b)
{
    switch (b->view.toolbar_focus)
    {
    case PLAYER_BROWSER_CATEGORIES:
        modal(b, PLAYER_BROWSER_MODAL_CATEGORIES, b->filter_count);
        break;
    case PLAYER_BROWSER_FAVORITES:
        return action(PLAYER_BROWSER_ACTION_FILTER, 1);
    case PLAYER_BROWSER_RECENT:
        return action(PLAYER_BROWSER_ACTION_FILTER, 2);
    case PLAYER_BROWSER_SEARCH:
        return action(PLAYER_BROWSER_ACTION_SEARCH, 0);
    case PLAYER_BROWSER_SOURCE_FILTER:
        modal(b, PLAYER_BROWSER_MODAL_SOURCES, b->source_count + 2);
        break;
    }
    return none();
}

static void back(PlayerBrowser *b)
{
    if (b->view.modal)
        b->view.modal = PLAYER_BROWSER_MODAL_NONE;
    else if (b->view.page == PLAYER_BROWSER_SOURCES)
    {
        b->view.page = b->return_page;
        b->view.selected_index = b->return_selected_index;
        b->view.item_count = b->return_item_count;
        b->view.scroll_offset = b->return_scroll_offset;
        b->view.focus = PLAYER_BROWSER_FOCUS_ROWS;
    }
    else if (b->view.page == PLAYER_BROWSER_LIBRARY && b->view.playback_active)
        b->view.page = PLAYER_BROWSER_DRAWER;
    else
        b->view.page = PLAYER_BROWSER_CLOSED;
    b->velocity = 0;
    b->touching = false;
    b->suppress_held = true;
    window(b);
}

static PlayerBrowserAction activate(PlayerBrowser *b)
{
    PlayerBrowserView *v = &b->view;
    if (v->modal)
    {
        if (!v->modal_count)
            return none();
        int i = v->modal_cursor;
        PlayerBrowserModal m = v->modal;
        v->modal = PLAYER_BROWSER_MODAL_NONE;
        b->suppress_held = true;
        if (m == PLAYER_BROWSER_MODAL_CATEGORIES)
            return action(PLAYER_BROWSER_ACTION_FILTER, i);
        if (m == PLAYER_BROWSER_MODAL_DELETE)
            return i == 1 ? action(PLAYER_BROWSER_ACTION_DELETE, v->selected_index) : none();
        if (i == 1)
        {
            if (v->page != PLAYER_BROWSER_SOURCES)
            {
                b->return_page = v->page;
                b->return_selected_index = v->selected_index;
                b->return_item_count = v->item_count;
                b->return_scroll_offset = v->scroll_offset;
            }
            v->page = PLAYER_BROWSER_SOURCES;
            v->focus = PLAYER_BROWSER_FOCUS_ROWS;
            v->scroll_offset = 0;
            v->action_focus = 0;
            return none();
        }
        return action(PLAYER_BROWSER_ACTION_SOURCE_FILTER, i == 0 ? -1 : i - 2);
    }
    if (v->focus == PLAYER_BROWSER_FOCUS_TOOLBAR)
        return toolbar(b);
    if (v->page == PLAYER_BROWSER_SOURCES)
    {
        if (v->focus == PLAYER_BROWSER_FOCUS_ROWS)
        {
            v->focus = PLAYER_BROWSER_FOCUS_ACTIONS;
            return none();
        }
        static const PlayerBrowserActionKind kinds[] = {
            PLAYER_BROWSER_ACTION_ADD_URL, PLAYER_BROWSER_ACTION_SCAN_SD,
            PLAYER_BROWSER_ACTION_REFRESH, PLAYER_BROWSER_ACTION_EPG, PLAYER_BROWSER_ACTION_DELETE};
        if (v->action_focus >= 2 && !v->item_count)
            return none();
        if (v->action_focus == 4)
        {
            modal(b, PLAYER_BROWSER_MODAL_DELETE, 2);
            return none();
        }
        return action(kinds[v->action_focus], v->selected_index);
    }
    if (v->focus == PLAYER_BROWSER_FOCUS_ACTIONS && v->action_focus == 1)
        return action(PLAYER_BROWSER_ACTION_FAVORITE, v->selected_index);
    return v->item_count ? action(PLAYER_BROWSER_ACTION_PLAY, v->selected_index) : none();
}

static void navigate(PlayerBrowser *b, unsigned d)
{
    PlayerBrowserView *v = &b->view;
    if (v->modal)
    {
        int delta = d == PLAYER_BROWSER_UP     ? -columns(v)
                    : d == PLAYER_BROWSER_DOWN ? columns(v)
                    : d == PLAYER_BROWSER_LEFT ? -1
                                               : 1;
        v->modal_cursor =
            (int)clamp(v->modal_cursor + delta, 0, v->modal_count > 0 ? v->modal_count - 1 : 0);
        reveal(b);
        return;
    }
    if (v->focus == PLAYER_BROWSER_FOCUS_TOOLBAR)
    {
        if (d == PLAYER_BROWSER_LEFT || d == PLAYER_BROWSER_RIGHT)
            v->toolbar_focus = (v->toolbar_focus + (d == PLAYER_BROWSER_LEFT ? 4 : 1)) % 5;
        else if (d == PLAYER_BROWSER_DOWN)
            v->focus = PLAYER_BROWSER_FOCUS_ROWS;
    }
    else if (v->focus == PLAYER_BROWSER_FOCUS_ACTIONS)
    {
        int n = player_browser_action_count(v);
        if (d == PLAYER_BROWSER_LEFT || d == PLAYER_BROWSER_RIGHT)
            v->action_focus = (v->action_focus + (d == PLAYER_BROWSER_LEFT ? n - 1 : 1)) % n;
        else if (d == PLAYER_BROWSER_UP)
            v->focus = PLAYER_BROWSER_FOCUS_ROWS;
    }
    else
    {
        if (d == PLAYER_BROWSER_LEFT)
            v->focus = PLAYER_BROWSER_FOCUS_TOOLBAR;
        else if (d == PLAYER_BROWSER_RIGHT)
            v->focus = PLAYER_BROWSER_FOCUS_ACTIONS;
        else if (d == PLAYER_BROWSER_UP && v->selected_index == 0)
            v->focus = PLAYER_BROWSER_FOCUS_TOOLBAR;
        else if (d == PLAYER_BROWSER_DOWN && v->selected_index >= v->item_count - 1)
            v->focus = PLAYER_BROWSER_FOCUS_ACTIONS;
        else
        {
            v->selected_index += d == PLAYER_BROWSER_UP ? -1 : 1;
            reveal(b);
        }
    }
}

void player_browser_tick(PlayerBrowser *b, uint64_t now)
{
    float dt = b->last_tick ? (now - b->last_tick) / 1000.0f : 0;
    b->last_tick = now;
    dt = fminf(dt, 0.05f);
    if (!b->touching && fabsf(b->velocity) > 2)
    {
        float *off = b->view.modal ? &b->view.modal_scroll_offset : &b->view.scroll_offset;
        /* Integrate exponential friction so distance is independent of FPS. */
        float decay = expf(-3.8f * dt);
        *off += b->velocity * (1 - decay) / 3.8f;
        b->velocity *= decay;
        window(b);
        if (*off <= 0 || *off >= maximum(&b->view))
            b->velocity = 0;
    }
}

PlayerBrowserAction player_browser_input(PlayerBrowser *b, unsigned pressed, unsigned held,
                                         uint64_t now)
{
    if (!b->view.page)
        return none();
    if (pressed & PLAYER_BROWSER_BACK)
    {
        back(b);
        return none();
    }
    if (pressed & PLAYER_BROWSER_EXPAND)
    {
        if (!b->view.modal && b->view.page == PLAYER_BROWSER_DRAWER)
        {
            b->view.page = PLAYER_BROWSER_LIBRARY;
            reveal(b);
        }
        return none();
    }
    if (pressed & PLAYER_BROWSER_ACCEPT)
    {
        b->velocity = 0;
        return activate(b);
    }
    if ((pressed & PLAYER_BROWSER_FAVORITE_KEY) && !b->view.modal &&
        b->view.page != PLAYER_BROWSER_SOURCES && b->view.item_count)
        return action(PLAYER_BROWSER_ACTION_FAVORITE, b->view.selected_index);
    unsigned d = held & 15;
    d = d & (~d + 1);
    if (b->suppress_held)
    {
        b->held_direction = 0;
        if (d)
            return none();
        b->suppress_held = false;
    }
    if (!d)
    {
        b->held_direction = 0;
        return none();
    }
    if (b->held_direction != (int)d)
    {
        b->held_direction = d;
        b->held_since = now;
        b->next_repeat = now + 350;
        navigate(b, d);
        b->velocity = 0;
    }
    else if (now >= b->next_repeat)
    {
        navigate(b, d);
        b->next_repeat = now + (now - b->held_since > 1800  ? 35
                                : now - b->held_since > 800 ? 70
                                                            : 130);
        b->velocity = 0;
    }
    return none();
}

PlayerBrowserAction player_browser_touch(PlayerBrowser *b, bool down, float x, float y,
                                         uint64_t now)
{
    PlayerBrowserView *v = &b->view;
    if (!v->page)
    {
        b->touching = false;
        return none();
    }
    PlayerBrowserRect list =
        v->modal ? (PlayerBrowserRect){192, 180, 896, 336} : player_browser_list_rect(v);
    float *off = v->modal ? &v->modal_scroll_offset : &v->scroll_offset;
    if (down)
    {
        if (!b->touching)
        {
            b->touching = true;
            b->dragging = false;
            b->touch_start_x = x;
            b->touch_start_y = y;
            b->touch_last_y = y;
            b->touch_time = now;
            b->velocity = 0;
            b->scrollbar_drag = hit(player_browser_scrollbar_rect(v, false), x, y);
        }
        if (fabsf(y - b->touch_start_y) > 10 || fabsf(x - b->touch_start_x) > 10)
            b->dragging = true;
        if (b->scrollbar_drag)
        {
            PlayerBrowserRect track = player_browser_scrollbar_rect(v, false),
                              thumb = player_browser_scrollbar_rect(v, true);
            *off =
                maximum(v) * clamp((y - track.y - thumb.h / 2) / fmaxf(1, track.h - thumb.h), 0, 1);
            b->dragging = true;
        }
        else if (b->dragging && hit(list, b->touch_start_x, b->touch_start_y))
        {
            float dy = b->touch_last_y - y;
            *off += dy;
            if (now > b->touch_time)
            {
                float dt = (now - b->touch_time) / 1000.0f;
                float sample = clamp(dy / dt, -6000, 6000);
                /* Smooth frame jitter, but reset immediately on reversal. */
                if (sample * b->velocity < 0)
                    b->velocity = 0;
                b->velocity += (sample - b->velocity) * (1 - expf(-dt / 0.04f));
            }
        }
        b->touch_last_y = y;
        b->touch_time = now;
        window(b);
        return none();
    }
    if (!b->touching)
        return none();
    b->touching = false;
    if (b->dragging || b->scrollbar_drag)
    {
        if (b->scrollbar_drag || now - b->touch_time > 100)
            b->velocity = 0;
        else
        {
            /* Slow positioning stays precise; a fast flick travels farther. */
            float boost = 1 + 1.5f * clamp((fabsf(b->velocity) - 600) / 2400, 0, 1);
            b->velocity = clamp(b->velocity * boost, -9000, 9000);
        }
        return none();
    }
    x = b->touch_start_x;
    y = b->touch_start_y;
    if (v->modal)
    {
        if (!hit(player_browser_modal_rect(v), x, y))
        {
            back(b);
            return none();
        }
        int end = v->modal_first_index + 5 * columns(v);
        if (end > v->modal_count)
            end = v->modal_count;
        if (hit(list, x, y))
            for (int i = v->modal_first_index; i < end; i++)
                if (hit(player_browser_modal_item_rect(v, i), x, y))
                {
                    v->modal_cursor = i;
                    return activate(b);
                }
        return none();
    }
    if (v->page == PLAYER_BROWSER_DRAWER && !hit(player_browser_panel_rect(v), x, y))
    {
        back(b);
        return none();
    }
    if (hit(player_browser_close_rect(v), x, y))
    {
        back(b);
        return none();
    }
    if (hit(player_browser_expand_rect(v), x, y))
    {
        if (v->page == PLAYER_BROWSER_DRAWER)
            v->page = PLAYER_BROWSER_LIBRARY;
        return none();
    }
    for (int i = 0; i < 5; i++)
        if (hit(player_browser_toolbar_rect(v, i), x, y))
        {
            v->toolbar_focus = i;
            v->focus = PLAYER_BROWSER_FOCUS_TOOLBAR;
            return toolbar(b);
        }
    for (int i = 0; i < player_browser_action_count(v); i++)
        if (hit(player_browser_action_rect(v, i), x, y))
        {
            v->action_focus = i;
            v->focus = PLAYER_BROWSER_FOCUS_ACTIONS;
            return activate(b);
        }
    if (hit(list, x, y))
    {
        int i = (int)((y - list.y + v->scroll_offset) / row_height(v));
        if (i < v->item_count)
        {
            v->selected_index = i;
            v->focus = PLAYER_BROWSER_FOCUS_ROWS;
        }
    }
    return none();
}
