#include "player/ui/bar.h"

#include <stdio.h>
#include <string.h>

#include "player/ui/overlay.h"
#include "player/ui/timeline.h"

#define PLAYER_UI_OSD_LONG_MS 600000

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

void player_ui_bar_build(const PlayerSnapshot *snapshot, const char *headline, PlayerUiOverlayBar *out)
{
    char timeline_text[80];
    int progress_permille = 0;

    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    snprintf(out->title, sizeof(out->title), "%s", headline ? headline : "Playing");

    if (!snapshot || !snapshot->has_media)
        return;

    player_ui_timeline_describe(snapshot, timeline_text, sizeof(timeline_text));
    snprintf(out->left, sizeof(out->left), "A Pause  L/R Seek");
    snprintf(out->center, sizeof(out->center), "%s", timeline_text);
    snprintf(out->right, sizeof(out->right), "Vol %d%%", snapshot->volume);

    if (snapshot->duration_ms > 0)
        progress_permille = clamp_int((snapshot->position_ms * 1000) / snapshot->duration_ms, 0, 1000);
    out->progress_permille = progress_permille;
}

int player_ui_bar_show(const PlayerSnapshot *snapshot, const char *headline, int duration_ms)
{
    PlayerUiOverlayBar bar;

    if (!snapshot || !snapshot->has_media)
        return player_ui_overlay_show_message(headline ? headline : "", NULL, duration_ms);

    player_ui_bar_build(snapshot, headline, &bar);
    return player_ui_overlay_show_bar(&bar, duration_ms);
}

int player_ui_bar_show_help(const PlayerSnapshot *snapshot, bool paused)
{
    return player_ui_bar_show(snapshot, paused ? "Paused" : "Playing", paused ? PLAYER_UI_OSD_LONG_MS : 2200);
}
