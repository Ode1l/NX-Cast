#include "player/ui/overlay.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "player/player.h"

static PlayerUiOverlaySnapshot g_overlay_snapshot;

static void overlay_reset_snapshot(void)
{
    memset(&g_overlay_snapshot, 0, sizeof(g_overlay_snapshot));
    g_overlay_snapshot.kind = PLAYER_UI_OVERLAY_NONE;
}

static void overlay_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void compose_bar_fallback_text(const PlayerUiOverlayBar *bar, char *text, size_t text_size)
{
    int progress_cells = 18;
    int filled = 0;
    int i = 0;
    char progress[32];
    char slots[220];
    char *cursor = progress;
    size_t remaining = sizeof(progress);

    if (!text || text_size == 0)
        return;

    text[0] = '\0';
    if (!bar)
        return;

    filled = clamp_int((bar->progress_permille * progress_cells + 500) / 1000, 0, progress_cells);
    if (remaining > 1)
    {
        *cursor++ = '[';
        --remaining;
    }
    for (i = 0; i < progress_cells && remaining > 1; ++i)
    {
        *cursor++ = i < filled ? '=' : '-';
        --remaining;
    }
    if (remaining > 1)
    {
        *cursor++ = ']';
        --remaining;
    }
    *cursor = '\0';

    snprintf(slots, sizeof(slots), "%s | %s | %s", bar->left, bar->center, bar->right);
    snprintf(text, text_size, "%s\n%s\n%s", bar->title, slots, progress);
}

int player_ui_overlay_show_message(const char *title, const char *line1, int duration_ms)
{
    char text[256];

    overlay_reset_snapshot();
    overlay_copy_string(g_overlay_snapshot.message.title, sizeof(g_overlay_snapshot.message.title), title);
    overlay_copy_string(g_overlay_snapshot.message.line1, sizeof(g_overlay_snapshot.message.line1), line1);
    if (!g_overlay_snapshot.message.title[0] && !g_overlay_snapshot.message.line1[0])
        return player_ui_overlay_show_text("", 0);

    g_overlay_snapshot.kind = PLAYER_UI_OVERLAY_MESSAGE;
    g_overlay_snapshot.duration_ms = duration_ms;

    if (g_overlay_snapshot.message.title[0] && g_overlay_snapshot.message.line1[0])
        snprintf(text, sizeof(text), "%s\n%s", g_overlay_snapshot.message.title, g_overlay_snapshot.message.line1);
    else if (g_overlay_snapshot.message.title[0])
        snprintf(text, sizeof(text), "%s", g_overlay_snapshot.message.title);
    else if (g_overlay_snapshot.message.line1[0])
        snprintf(text, sizeof(text), "%s", g_overlay_snapshot.message.line1);
    else
        text[0] = '\0';

    return player_ui_overlay_show_text(text, duration_ms);
}

int player_ui_overlay_show_bar(const PlayerUiOverlayBar *bar, int duration_ms)
{
    char text[320];

    overlay_reset_snapshot();
    g_overlay_snapshot.kind = PLAYER_UI_OVERLAY_BAR;
    g_overlay_snapshot.duration_ms = duration_ms;
    if (bar)
    {
        g_overlay_snapshot.bar = *bar;
        g_overlay_snapshot.bar.progress_permille = clamp_int(g_overlay_snapshot.bar.progress_permille, 0, 1000);
    }

    compose_bar_fallback_text(&g_overlay_snapshot.bar, text, sizeof(text));
    return player_ui_overlay_show_text(text, duration_ms);
}

int player_ui_overlay_show_text(const char *text, int duration_ms)
{
    if (!text)
        return 0;
    if (!text[0] && duration_ms <= 0)
    {
        overlay_reset_snapshot();
        (void)player_show_osd("", 0);
        return 0;
    }

    (void)player_show_osd(text, duration_ms);
    return duration_ms > 0 ? duration_ms : 0;
}

bool player_ui_overlay_get_snapshot(PlayerUiOverlaySnapshot *out)
{
    if (!out)
        return false;

    *out = g_overlay_snapshot;
    return g_overlay_snapshot.kind != PLAYER_UI_OVERLAY_NONE;
}

void player_ui_overlay_clear(void)
{
    overlay_reset_snapshot();
    (void)player_show_osd("", 0);
}
