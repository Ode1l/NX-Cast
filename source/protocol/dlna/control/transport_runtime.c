#include "transport_runtime.h"

#include <stdio.h>
#include <string.h>

#include "player/player.h"

const char *dlna_transport_state_from_player_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_IDLE:
        return "NO_MEDIA_PRESENT";
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return "TRANSITIONING";
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
}

const char *dlna_transport_status_from_player_state(PlayerState state)
{
    return state == PLAYER_STATE_ERROR ? "ERROR_OCCURRED" : "OK";
}

void dlna_format_hhmmss_from_ms(int value_ms, char *out, size_t out_size)
{
    int total_seconds;
    int hour;
    int minute;
    int second;

    if (!out || out_size == 0)
        return;

    if (value_ms < 0)
        value_ms = 0;

    total_seconds = value_ms / 1000;
    hour = total_seconds / 3600;
    minute = (total_seconds % 3600) / 60;
    second = total_seconds % 60;
    snprintf(out, out_size, "%02d:%02d:%02d", hour, minute, second);
}

void dlna_transport_get_snapshot(PlayerSnapshot *snapshot, const SoapRuntimeState *runtime_state)
{
    if (!snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    if (player_get_snapshot(snapshot))
        return;

    snapshot->state = player_get_state();
    snapshot->position_ms = player_get_position_ms();
    snapshot->duration_ms = player_get_duration_ms();
    snapshot->volume = player_get_volume();
    snapshot->mute = player_get_mute();
    snapshot->seekable = player_is_seekable();
    snapshot->has_media = runtime_state && runtime_state->transport_uri[0] != '\0';
}

unsigned int dlna_transport_current_actions(const PlayerSnapshot *snapshot)
{
    unsigned int actions = 0;

    if (!snapshot || !snapshot->has_media)
        return 0;

    switch (snapshot->state)
    {
    case PLAYER_STATE_STOPPED:
        actions |= DLNA_TRANSPORT_ACTION_PLAY | DLNA_TRANSPORT_ACTION_STOP;
        break;
    case PLAYER_STATE_LOADING:
        actions |= DLNA_TRANSPORT_ACTION_PLAY | DLNA_TRANSPORT_ACTION_PAUSE | DLNA_TRANSPORT_ACTION_STOP;
        break;
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        actions |= DLNA_TRANSPORT_ACTION_PAUSE | DLNA_TRANSPORT_ACTION_STOP;
        if (snapshot->seekable)
            actions |= DLNA_TRANSPORT_ACTION_SEEK;
        break;
    case PLAYER_STATE_PLAYING:
        actions |= DLNA_TRANSPORT_ACTION_PAUSE | DLNA_TRANSPORT_ACTION_STOP;
        if (snapshot->seekable)
            actions |= DLNA_TRANSPORT_ACTION_SEEK;
        break;
    case PLAYER_STATE_PAUSED:
        actions |= DLNA_TRANSPORT_ACTION_PLAY | DLNA_TRANSPORT_ACTION_STOP;
        if (snapshot->seekable)
            actions |= DLNA_TRANSPORT_ACTION_SEEK;
        break;
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_ERROR:
    default:
        break;
    }

    return actions;
}

bool dlna_transport_action_available(unsigned int actions, DlnaTransportActionMask action)
{
    return (actions & (unsigned int)action) != 0;
}

void dlna_transport_format_actions(unsigned int actions, char *out, size_t out_size)
{
    bool first = true;
    size_t used = 0;

    if (!out || out_size == 0)
        return;

    out[0] = '\0';

    if (dlna_transport_action_available(actions, DLNA_TRANSPORT_ACTION_PLAY))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sPlay", first ? "" : ",");
        first = false;
    }
    if (used < out_size && dlna_transport_action_available(actions, DLNA_TRANSPORT_ACTION_STOP))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sStop", first ? "" : ",");
        first = false;
    }
    if (used < out_size && dlna_transport_action_available(actions, DLNA_TRANSPORT_ACTION_PAUSE))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sPause", first ? "" : ",");
        first = false;
    }
    if (used < out_size && dlna_transport_action_available(actions, DLNA_TRANSPORT_ACTION_SEEK))
        (void)snprintf(out + used, out_size - used, "%sSeek", first ? "" : ",");
}

void dlna_transport_sync_runtime_from_snapshot(SoapRuntimeState *runtime_state)
{
    PlayerSnapshot snapshot;

    if (!runtime_state)
        return;

    if (!player_get_snapshot(&snapshot))
        return;

    snprintf(runtime_state->transport_state, sizeof(runtime_state->transport_state), "%s",
             dlna_transport_state_from_player_state(snapshot.state));
    snprintf(runtime_state->transport_status, sizeof(runtime_state->transport_status), "%s",
             dlna_transport_status_from_player_state(snapshot.state));
    dlna_format_hhmmss_from_ms(snapshot.duration_ms,
                               runtime_state->transport_duration,
                               sizeof(runtime_state->transport_duration));
    dlna_format_hhmmss_from_ms(snapshot.position_ms,
                               runtime_state->transport_rel_time,
                               sizeof(runtime_state->transport_rel_time));
    dlna_format_hhmmss_from_ms(snapshot.position_ms,
                               runtime_state->transport_abs_time,
                               sizeof(runtime_state->transport_abs_time));
    runtime_state->volume = snapshot.volume;
    runtime_state->mute = snapshot.mute;
}

void dlna_transport_sync_runtime_on_event(SoapRuntimeState *runtime_state, const PlayerEvent *event)
{
    if (!runtime_state || !event)
        return;

    switch (event->type)
    {
    case PLAYER_EVENT_STATE_CHANGED:
        snprintf(runtime_state->transport_state, sizeof(runtime_state->transport_state), "%s",
                 dlna_transport_state_from_player_state(event->state));
        snprintf(runtime_state->transport_status, sizeof(runtime_state->transport_status), "%s",
                 dlna_transport_status_from_player_state(event->state));
        break;
    case PLAYER_EVENT_POSITION_CHANGED:
        dlna_format_hhmmss_from_ms(event->position_ms,
                                   runtime_state->transport_rel_time,
                                   sizeof(runtime_state->transport_rel_time));
        dlna_format_hhmmss_from_ms(event->position_ms,
                                   runtime_state->transport_abs_time,
                                   sizeof(runtime_state->transport_abs_time));
        break;
    case PLAYER_EVENT_DURATION_CHANGED:
        dlna_format_hhmmss_from_ms(event->duration_ms,
                                   runtime_state->transport_duration,
                                   sizeof(runtime_state->transport_duration));
        break;
    case PLAYER_EVENT_VOLUME_CHANGED:
        runtime_state->volume = event->volume;
        break;
    case PLAYER_EVENT_MUTE_CHANGED:
        runtime_state->mute = event->mute;
        break;
    case PLAYER_EVENT_URI_CHANGED:
        if (event->uri)
            snprintf(runtime_state->transport_uri, sizeof(runtime_state->transport_uri), "%s", event->uri);
        break;
    case PLAYER_EVENT_ERROR:
        snprintf(runtime_state->transport_status, sizeof(runtime_state->transport_status), "ERROR_OCCURRED");
        break;
    default:
        break;
    }
}
