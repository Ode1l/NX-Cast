#include "player/ui/controls.h"

#include <stdio.h>

#include <switch.h>

#include "player/ui/bar.h"
#include "player/ui/overlay.h"

#define PLAYER_UI_OSD_SHORT_MS 1500

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static uint64_t monotonic_time_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

int player_ui_controls_show(const PlayerSnapshot *snapshot, const char *headline, int duration_ms)
{
    return player_ui_bar_show(snapshot, headline, duration_ms);
}

int player_ui_controls_show_help(const PlayerSnapshot *snapshot, bool paused)
{
    return player_ui_bar_show_help(snapshot, paused);
}

bool player_ui_controls_toggle_pause(PlayerUiState *state, const PlayerSnapshot *snapshot)
{
    bool ok = false;
    int duration = 0;

    if (!snapshot || !snapshot->has_media)
        return false;

    if (snapshot->state == PLAYER_STATE_PLAYING || snapshot->state == PLAYER_STATE_BUFFERING || snapshot->state == PLAYER_STATE_SEEKING)
    {
        ok = player_pause();
        if (ok)
            duration = player_ui_controls_show_help(snapshot, true);
    }
    else
    {
        ok = player_play();
        if (ok)
            duration = player_ui_controls_show_help(snapshot, false);
    }

    if (!ok)
        duration = player_ui_overlay_show_message("Play/Pause failed", NULL, PLAYER_UI_OSD_SHORT_MS);

    if (state)
        state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
    return ok;
}

bool player_ui_controls_seek(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta_ms)
{
    int target_ms;
    int duration_ms;
    int duration;
    char headline[48];

    if (!snapshot || !snapshot->has_media || !snapshot->seekable)
    {
        duration = player_ui_overlay_show_message("Seek unavailable", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
        return false;
    }

    target_ms = snapshot->position_ms + delta_ms;
    duration_ms = snapshot->duration_ms;
    if (target_ms < 0)
        target_ms = 0;
    if (duration_ms > 0 && target_ms > duration_ms)
        target_ms = duration_ms;

    if (!player_seek_ms(target_ms))
    {
        duration = player_ui_overlay_show_message("Seek failed", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
        return false;
    }

    snprintf(headline, sizeof(headline), "Seek %s%d s", delta_ms >= 0 ? "+" : "", delta_ms / 1000);
    duration = player_ui_controls_show(snapshot, headline, PLAYER_UI_OSD_SHORT_MS);
    if (state)
        state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
    return true;
}

bool player_ui_controls_change_volume(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta)
{
    int current_volume;
    int target_volume;
    int duration;
    char headline[64];

    if (!snapshot)
        return false;

    current_volume = snapshot->volume;
    if (snapshot->mute && delta > 0 && current_volume <= 0)
        current_volume = PLAYER_UI_VOLUME_STEP;

    target_volume = clamp_int(current_volume + delta, 0, 100);
    if (snapshot->mute && target_volume > 0)
        (void)player_set_mute(false);

    if (!player_set_volume(target_volume))
    {
        duration = player_ui_overlay_show_message("Volume failed", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
        return false;
    }

    snprintf(headline, sizeof(headline), "Volume %d%%", target_volume);
    duration = player_ui_controls_show(snapshot, headline, PLAYER_UI_OSD_SHORT_MS);
    if (state)
        state->overlay_until_ms = monotonic_time_ms() + (uint64_t)duration;
    return true;
}
