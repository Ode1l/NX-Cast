#include "player/ui/bar.h"

#include <stdio.h>
#include <string.h>

#include "player/ui/overlay.h"
#include "player/ui/timeline.h"

#define PLAYER_UI_OSD_LONG_MS 600000

static const char *state_label(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_LOADING:
        return "LOADING";
    case PLAYER_STATE_BUFFERING:
        return "BUFFERING";
    case PLAYER_STATE_SEEKING:
        return "SEEKING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED";
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_ERROR:
        return "ERROR";
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_IDLE:
    default:
        return "READY";
    }
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static bool has_prefix(const char *text, const char *prefix)
{
    if (!text || !prefix)
        return false;
    while (*prefix)
    {
        if (*text++ != *prefix++)
            return false;
    }
    return true;
}

static int parse_seek_delta_ms(const char *headline)
{
    int sign = 1;
    int seconds = 0;

    if (!has_prefix(headline, "SEEK "))
        return 0;

    headline += 5;
    if (*headline == '-')
    {
        sign = -1;
        ++headline;
    }
    else if (*headline == '+')
    {
        ++headline;
    }
    else
    {
        return 0;
    }

    while (*headline >= '0' && *headline <= '9')
    {
        seconds = seconds * 10 + (*headline - '0');
        ++headline;
    }

    return sign * seconds * 1000;
}

static PlayerUiOverlayFocus focus_from_headline(const char *headline, PlayerState state)
{
    if (has_prefix(headline, "SEEK"))
        return PLAYER_UI_OVERLAY_FOCUS_SEEK;
    if (has_prefix(headline, "VOLUME"))
        return PLAYER_UI_OVERLAY_FOCUS_VOLUME;
    if (has_prefix(headline, "PAUSED"))
        return PLAYER_UI_OVERLAY_FOCUS_PLAY;
    if (has_prefix(headline, "PLAYING"))
        return PLAYER_UI_OVERLAY_FOCUS_PAUSE;

    switch (state)
    {
    case PLAYER_STATE_PAUSED:
        return PLAYER_UI_OVERLAY_FOCUS_PLAY;
    case PLAYER_STATE_PLAYING:
        return PLAYER_UI_OVERLAY_FOCUS_PAUSE;
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
    case PLAYER_STATE_ERROR:
        return PLAYER_UI_OVERLAY_FOCUS_STATUS;
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
    default:
        return PLAYER_UI_OVERLAY_FOCUS_NONE;
    }
}

void player_ui_bar_build(const PlayerSnapshot *snapshot, const char *headline, PlayerUiOverlayBar *out)
{
    char position_text[16];
    char duration_text[16];
    const char *label;

    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->state = PLAYER_STATE_IDLE;
    snprintf(out->title, sizeof(out->title), "%s", headline ? headline : "PLAYING");

    if (!snapshot || !snapshot->has_media)
        return;

    label = state_label(snapshot->state);
    out->state = snapshot->state;
    out->position_ms = snapshot->position_ms;
    out->duration_ms = snapshot->duration_ms;
    out->volume = clamp_int(snapshot->volume, 0, 100);
    out->mute = snapshot->mute;
    out->seekable = snapshot->seekable;
    out->progress_permille = player_ui_timeline_progress_permille(snapshot);

    player_ui_timeline_format_time(snapshot->position_ms, position_text, sizeof(position_text));
    if (snapshot->duration_ms > 0)
        player_ui_timeline_format_time(snapshot->duration_ms, duration_text, sizeof(duration_text));
    else
        snprintf(duration_text, sizeof(duration_text), "--:--");

    if (!headline)
        snprintf(out->title, sizeof(out->title), "%s", label);
    out->focus = focus_from_headline(out->title, snapshot->state);
    out->seek_delta_ms = parse_seek_delta_ms(out->title);
    out->subtitle[0] = '\0';
    snprintf(out->left, sizeof(out->left), out->focus == PLAYER_UI_OVERLAY_FOCUS_PLAY ? "A PLAY" : "A PAUSE");
    snprintf(out->center, sizeof(out->center), "%s / %s", position_text, duration_text);
    snprintf(out->right, sizeof(out->right), snapshot->mute ? "MUTE" : "VOL %d%%", out->volume);
    snprintf(out->hint, sizeof(out->hint), "L/R SEEK   TOUCH DRAG   UP/DOWN VOL");
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
    return player_ui_bar_show(snapshot, paused ? "PAUSED" : "PLAYING", paused ? PLAYER_UI_OSD_LONG_MS : 2200);
}
