#include "player/ui/controls.h"

#include <stdio.h>

#include <switch.h>

#include "player/ui/bar.h"
#include "player/ui/overlay.h"

#define PLAYER_UI_OSD_SHORT_MS 1500
#define PLAYER_UI_SEEK_PREVIEW_MS 1200
#define PLAYER_UI_INTERACTION_HOLD_MS 1200

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

static bool submit_ui_command(PlayerCommandKind kind, int value, bool flag)
{
    PlayerCommandRequest request = {
        .kind = kind,
        .source = PLAYER_COMMAND_SOURCE_UI,
        .value = value,
        .flag = flag,
    };

    return player_command_status_succeeded(
        player_submit_command_async(&request));
}

static void update_overlay_timing(PlayerUiState *state, uint64_t now_ms, int duration, bool interaction)
{
    if (!state)
        return;

    state->overlay_until_ms = now_ms + (uint64_t)duration;
    state->overlay_refresh_at_ms = now_ms + PLAYER_UI_OVERLAY_REFRESH_MS;
    state->auto_overlay_suppressed_state = PLAYER_STATE_IDLE;
    state->auto_overlay_suppressed_until_ms = 0;
    if (interaction)
        state->interaction_overlay_until_ms = now_ms + PLAYER_UI_INTERACTION_HOLD_MS;
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
    uint64_t now_ms = monotonic_time_ms();

    if (!snapshot || !snapshot->has_media)
        return false;

    if (snapshot->state == PLAYER_STATE_PLAYING || snapshot->state == PLAYER_STATE_BUFFERING || snapshot->state == PLAYER_STATE_SEEKING)
    {
        ok = submit_ui_command(PLAYER_COMMAND_PAUSE, 0, false);
        if (ok)
            duration = player_ui_controls_show_help(snapshot, true);
    }
    else
    {
        ok = submit_ui_command(PLAYER_COMMAND_PLAY, 0, false);
        if (ok)
            duration = player_ui_controls_show_help(snapshot, false);
    }

    if (!ok)
        duration = player_ui_overlay_show_message("Play/Pause failed", NULL, PLAYER_UI_OSD_SHORT_MS);

    update_overlay_timing(state, now_ms, duration, true);
    return ok;
}

bool player_ui_controls_seek(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta_ms)
{
    int target_ms;
    int duration_ms;
    int duration;
    int base_ms;
    uint64_t now_ms = monotonic_time_ms();
    char headline[48];
    PlayerSnapshot preview_snapshot;

    if (!snapshot || !snapshot->has_media || !snapshot->seekable)
    {
        duration = player_ui_overlay_show_message("Seek unavailable", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->seek_preview_active = false;
        update_overlay_timing(state, now_ms, duration, true);
        return false;
    }

    base_ms = snapshot->position_ms;
    if (state && state->seek_preview_active && now_ms < state->seek_preview_until_ms)
        base_ms = state->seek_preview_ms;

    target_ms = base_ms + delta_ms;
    duration_ms = snapshot->duration_ms;
    if (target_ms < 0)
        target_ms = 0;
    if (duration_ms > 0 && target_ms > duration_ms)
        target_ms = duration_ms;

    if (!submit_ui_command(PLAYER_COMMAND_SEEK_MS, target_ms, false))
    {
        duration = player_ui_overlay_show_message("Seek failed", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->seek_preview_active = false;
        update_overlay_timing(state, now_ms, duration, true);
        return false;
    }

    snprintf(headline, sizeof(headline), "SEEK %s%dS", delta_ms >= 0 ? "+" : "", delta_ms / 1000);
    preview_snapshot = *snapshot;
    preview_snapshot.position_ms = target_ms;
    duration = player_ui_controls_show(&preview_snapshot, headline, PLAYER_UI_SEEK_PREVIEW_MS);
    if (state)
    {
        state->seek_preview_active = true;
        state->seek_preview_ms = target_ms;
        state->seek_preview_until_ms = now_ms + PLAYER_UI_SEEK_PREVIEW_MS;
    }
    update_overlay_timing(state, now_ms, duration, true);
    return true;
}

static bool player_ui_controls_show_seek_to(PlayerUiState *state,
                                            const PlayerSnapshot *snapshot,
                                            int target_ms,
                                            const char *headline,
                                            int duration_ms,
                                            bool interaction)
{
    PlayerSnapshot preview_snapshot;
    uint64_t now_ms = monotonic_time_ms();
    int duration;

    if (!snapshot || !snapshot->has_media || !snapshot->seekable || snapshot->duration_ms <= 0)
    {
        duration = player_ui_overlay_show_message("Seek unavailable", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->seek_preview_active = false;
        update_overlay_timing(state, now_ms, duration, interaction);
        return false;
    }

    target_ms = clamp_int(target_ms, 0, snapshot->duration_ms);
    preview_snapshot = *snapshot;
    preview_snapshot.position_ms = target_ms;
    duration = player_ui_controls_show(&preview_snapshot, headline ? headline : "SEEK", duration_ms);

    if (state)
    {
        state->seek_preview_active = true;
        state->seek_preview_ms = target_ms;
        state->seek_preview_until_ms = now_ms + (uint64_t)duration_ms;
    }
    update_overlay_timing(state, now_ms, duration, interaction);
    return true;
}

bool player_ui_controls_preview_seek_to(PlayerUiState *state, const PlayerSnapshot *snapshot, int target_ms)
{
    return player_ui_controls_show_seek_to(state,
                                           snapshot,
                                           target_ms,
                                           "SEEK",
                                           PLAYER_UI_SEEK_PREVIEW_MS,
                                           true);
}

bool player_ui_controls_seek_to(PlayerUiState *state, const PlayerSnapshot *snapshot, int target_ms)
{
    uint64_t now_ms = monotonic_time_ms();
    int duration;

    if (!snapshot || !snapshot->has_media || !snapshot->seekable || snapshot->duration_ms <= 0)
    {
        duration = player_ui_overlay_show_message("Seek unavailable", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->seek_preview_active = false;
        update_overlay_timing(state, now_ms, duration, true);
        return false;
    }

    target_ms = clamp_int(target_ms, 0, snapshot->duration_ms);
    if (!submit_ui_command(PLAYER_COMMAND_SEEK_MS, target_ms, false))
    {
        duration = player_ui_overlay_show_message("Seek failed", NULL, PLAYER_UI_OSD_SHORT_MS);
        if (state)
            state->seek_preview_active = false;
        update_overlay_timing(state, now_ms, duration, true);
        return false;
    }

    return player_ui_controls_show_seek_to(state,
                                           snapshot,
                                           target_ms,
                                           "SEEK",
                                           PLAYER_UI_SEEK_PREVIEW_MS,
                                           true);
}

bool player_ui_controls_change_volume(PlayerUiState *state, const PlayerSnapshot *snapshot, int delta)
{
    int current_volume;
    int target_volume;
    int duration;
    uint64_t now_ms = monotonic_time_ms();
    char headline[64];
    PlayerSnapshot preview_snapshot;

    if (!snapshot)
        return false;

    current_volume = snapshot->volume;
    if (snapshot->mute && delta > 0 && current_volume <= 0)
        current_volume = PLAYER_UI_VOLUME_STEP;

    target_volume = clamp_int(current_volume + delta, 0, 100);
    if (snapshot->mute && target_volume > 0)
        (void)submit_ui_command(PLAYER_COMMAND_SET_MUTE, 0, false);

    if (!submit_ui_command(PLAYER_COMMAND_SET_VOLUME, target_volume, false))
    {
        duration = player_ui_overlay_show_message("Volume failed", NULL, PLAYER_UI_OSD_SHORT_MS);
        update_overlay_timing(state, now_ms, duration, true);
        return false;
    }

    snprintf(headline, sizeof(headline), "VOLUME %d%%", target_volume);
    preview_snapshot = *snapshot;
    preview_snapshot.volume = target_volume;
    preview_snapshot.mute = false;
    duration = player_ui_controls_show(&preview_snapshot, headline, PLAYER_UI_OSD_SHORT_MS);
    update_overlay_timing(state, now_ms, duration, true);
    return true;
}
