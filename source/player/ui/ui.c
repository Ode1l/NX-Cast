#include "player/ui/ui.h"
#include "player/ui/bar.h"
#include "player/ui/controls.h"
#include "player/ui/overlay.h"

#include <string.h>

#include <switch.h>

#define PLAYER_UI_OSD_LONG_MS 600000
#define PLAYER_UI_OSD_REFRESH_MS 250

static uint64_t monotonic_time_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static int show_video_state_osd(const PlayerSnapshot *snapshot, bool first_video_frame)
{
    if (!snapshot || !snapshot->has_media)
        return player_ui_overlay_show_message("", NULL, 0);

    switch (snapshot->state)
    {
    case PLAYER_STATE_LOADING:
        return player_ui_overlay_show_message("Loading...", NULL, PLAYER_UI_OSD_LONG_MS);
    case PLAYER_STATE_BUFFERING:
        return player_ui_overlay_show_message("Buffering...", NULL, PLAYER_UI_OSD_LONG_MS);
    case PLAYER_STATE_SEEKING:
        return player_ui_overlay_show_message("Seeking...", NULL, PLAYER_UI_OSD_LONG_MS);
    case PLAYER_STATE_PAUSED:
        return player_ui_bar_show_help(snapshot, true);
    case PLAYER_STATE_PLAYING:
        if (first_video_frame)
            return player_ui_bar_show_help(snapshot, false);
        return player_ui_overlay_show_message("", NULL, 0);
    case PLAYER_STATE_ERROR:
        return player_ui_overlay_show_message("Playback error", NULL, 3000);
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
    default:
        return player_ui_overlay_show_message("", NULL, 0);
    }
}

void player_ui_reset(PlayerUiState *state)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
    state->last_state = PLAYER_STATE_IDLE;
}

void player_ui_clear(PlayerUiState *state)
{
    player_ui_overlay_clear();
    if (!state)
        return;

    state->video_active = false;
    state->last_state = PLAYER_STATE_IDLE;
    state->overlay_until_ms = 0;
    state->overlay_refresh_at_ms = 0;
}

void player_ui_sync(PlayerUiState *state, const PlayerSnapshot *snapshot)
{
    bool first_video_frame;
    bool persistent_state;
    bool overlay_active;
    bool refresh_due;
    uint64_t now_ms;
    int duration;

    if (!state)
        return;

    if (!snapshot || !snapshot->has_media)
    {
        if (state->video_active)
            player_ui_clear(state);
        return;
    }

    now_ms = monotonic_time_ms();
    first_video_frame = !state->video_active;
    persistent_state = snapshot->state == PLAYER_STATE_LOADING ||
                       snapshot->state == PLAYER_STATE_BUFFERING ||
                       snapshot->state == PLAYER_STATE_SEEKING ||
                       snapshot->state == PLAYER_STATE_PAUSED;
    overlay_active = snapshot->state == PLAYER_STATE_PAUSED || now_ms < state->overlay_until_ms;
    refresh_due = overlay_active && now_ms >= state->overlay_refresh_at_ms;

    if (!state->video_active ||
        snapshot->state != state->last_state ||
        (persistent_state && now_ms >= state->overlay_until_ms))
    {
        duration = show_video_state_osd(snapshot, first_video_frame);
        state->overlay_until_ms = now_ms + (uint64_t)duration;
        state->overlay_refresh_at_ms = now_ms + PLAYER_UI_OSD_REFRESH_MS;
    }
    else if (refresh_due)
    {
        duration = player_ui_controls_show_help(snapshot, snapshot->state == PLAYER_STATE_PAUSED);
        if (snapshot->state == PLAYER_STATE_PAUSED)
            state->overlay_until_ms = now_ms + (uint64_t)duration;
        state->overlay_refresh_at_ms = now_ms + PLAYER_UI_OSD_REFRESH_MS;
    }

    state->video_active = true;
    state->last_state = snapshot->state;
}

bool player_ui_toggle_pause(PlayerUiState *state, const PlayerSnapshot *snapshot)
{
    return player_ui_controls_toggle_pause(state, snapshot);
}

bool player_ui_seek(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta_ms)
{
    return player_ui_controls_seek(state, snapshot, delta_ms);
}

bool player_ui_change_volume(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta)
{
    return player_ui_controls_change_volume(state, snapshot, delta);
}

int player_ui_show_controls(PlayerUiState *state, const PlayerSnapshot *snapshot)
{
    int duration = player_ui_controls_show_help(snapshot, snapshot && snapshot->state == PLAYER_STATE_PAUSED);

    if (state)
    {
        uint64_t now_ms = monotonic_time_ms();
        state->overlay_until_ms = now_ms + (uint64_t)duration;
        state->overlay_refresh_at_ms = now_ms + PLAYER_UI_OSD_REFRESH_MS;
    }

    return duration;
}
