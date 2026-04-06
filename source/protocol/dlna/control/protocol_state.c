#include "protocol_state.h"

#include <stdio.h>
#include <string.h>

#include "log/log.h"

#define DLNA_DEFAULT_SINK_PROTOCOL_INFO \
    "http-get:*:audio/mpeg:*," \
    "http-get:*:audio/mp4:*," \
    "http-get:*:audio/x-m4a:*," \
    "http-get:*:audio/aac:*," \
    "http-get:*:audio/flac:*," \
    "http-get:*:audio/x-flac:*," \
    "http-get:*:audio/wav:*," \
    "http-get:*:audio/x-wav:*," \
    "http-get:*:audio/ogg:*," \
    "http-get:*:audio/vnd.dlna.adts:*," \
    "http-get:*:audio/x-mpegurl:*," \
    "http-get:*:audio/mpegurl:*," \
    "http-get:*:video/mp4:*," \
    "http-get:*:video/x-m4v:*," \
    "http-get:*:video/mpeg:*," \
    "http-get:*:video/mp2t:*," \
    "http-get:*:video/quicktime:*," \
    "http-get:*:video/webm:*," \
    "http-get:*:video/x-matroska:*," \
    "http-get:*:video/x-msvideo:*," \
    "http-get:*:video/vnd.dlna.mpeg-tts:*," \
    "http-get:*:video/m3u8:*," \
    "http-get:*:video/flv:*," \
    "http-get:*:video/x-flv:*," \
    "http-get:*:application/vnd.apple.mpegurl:*," \
    "http-get:*:application/x-mpegURL:*"

static DlnaProtocolState g_dlna_protocol_state;

const char *dlna_protocol_transport_state_from_player_state(PlayerState state)
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

const char *dlna_protocol_transport_status_from_player_state(PlayerState state)
{
    return state == PLAYER_STATE_ERROR ? "ERROR_OCCURRED" : "OK";
}

void dlna_protocol_format_hhmmss_from_ms(int value_ms, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;

    if (value_ms < 0)
        value_ms = 0;

    int total_seconds = value_ms / 1000;
    int hour = total_seconds / 3600;
    int minute = (total_seconds % 3600) / 60;
    int second = total_seconds % 60;
    snprintf(out, out_size, "%02d:%02d:%02d", hour, minute, second);
}

static void dlna_protocol_state_set_defaults(void)
{
    memset(&g_dlna_protocol_state, 0, sizeof(g_dlna_protocol_state));
    snprintf(g_dlna_protocol_state.transport_state, sizeof(g_dlna_protocol_state.transport_state), "NO_MEDIA_PRESENT");
    snprintf(g_dlna_protocol_state.transport_status, sizeof(g_dlna_protocol_state.transport_status), "OK");
    snprintf(g_dlna_protocol_state.transport_speed, sizeof(g_dlna_protocol_state.transport_speed), "1");
    snprintf(g_dlna_protocol_state.transport_duration, sizeof(g_dlna_protocol_state.transport_duration), "00:00:00");
    snprintf(g_dlna_protocol_state.transport_rel_time, sizeof(g_dlna_protocol_state.transport_rel_time), "00:00:00");
    snprintf(g_dlna_protocol_state.transport_abs_time, sizeof(g_dlna_protocol_state.transport_abs_time), "00:00:00");
    g_dlna_protocol_state.volume = PLAYER_DEFAULT_VOLUME;
    g_dlna_protocol_state.mute = false;
    g_dlna_protocol_state.source_protocol_info[0] = '\0';
    snprintf(g_dlna_protocol_state.sink_protocol_info,
             sizeof(g_dlna_protocol_state.sink_protocol_info),
             "%s",
             DLNA_DEFAULT_SINK_PROTOCOL_INFO);
    snprintf(g_dlna_protocol_state.connection_ids, sizeof(g_dlna_protocol_state.connection_ids), "0");
}

void dlna_protocol_state_init(void)
{
    dlna_protocol_state_set_defaults();
}

void dlna_protocol_state_reset(void)
{
    memset(&g_dlna_protocol_state, 0, sizeof(g_dlna_protocol_state));
}

const DlnaProtocolState *dlna_protocol_state_view(void)
{
    return &g_dlna_protocol_state;
}

void dlna_protocol_state_set_transport_uri(const char *uri, const char *metadata)
{
    if (uri)
        snprintf(g_dlna_protocol_state.transport_uri, sizeof(g_dlna_protocol_state.transport_uri), "%s", uri);
    if (metadata)
        snprintf(g_dlna_protocol_state.transport_uri_metadata,
                 sizeof(g_dlna_protocol_state.transport_uri_metadata),
                 "%s",
                 metadata);
}

void dlna_protocol_state_set_transport_status(const char *status)
{
    if (!status)
        return;
    snprintf(g_dlna_protocol_state.transport_status,
             sizeof(g_dlna_protocol_state.transport_status),
             "%s",
             status);
}

void dlna_protocol_state_set_transport_speed(const char *speed)
{
    if (!speed)
        return;
    snprintf(g_dlna_protocol_state.transport_speed, sizeof(g_dlna_protocol_state.transport_speed), "%s", speed);
}

void dlna_protocol_state_set_transport_timing(int duration_ms, int position_ms)
{
    dlna_protocol_format_hhmmss_from_ms(duration_ms,
                                        g_dlna_protocol_state.transport_duration,
                                        sizeof(g_dlna_protocol_state.transport_duration));
    dlna_protocol_format_hhmmss_from_ms(position_ms,
                                        g_dlna_protocol_state.transport_rel_time,
                                        sizeof(g_dlna_protocol_state.transport_rel_time));
    dlna_protocol_format_hhmmss_from_ms(position_ms,
                                        g_dlna_protocol_state.transport_abs_time,
                                        sizeof(g_dlna_protocol_state.transport_abs_time));
}

void dlna_protocol_state_set_source_protocol_info(const char *value)
{
    if (!value)
        return;
    snprintf(g_dlna_protocol_state.source_protocol_info,
             sizeof(g_dlna_protocol_state.source_protocol_info),
             "%s",
             value);
}

void dlna_protocol_state_set_sink_protocol_info(const char *value)
{
    if (!value)
        return;
    snprintf(g_dlna_protocol_state.sink_protocol_info,
             sizeof(g_dlna_protocol_state.sink_protocol_info),
             "%s",
             value);
}

void dlna_protocol_state_set_connection_ids(const char *value)
{
    if (!value)
        return;
    snprintf(g_dlna_protocol_state.connection_ids, sizeof(g_dlna_protocol_state.connection_ids), "%s", value);
}

void dlna_protocol_state_sync_from_player(void)
{
    PlayerSnapshot snapshot;

    if (!player_get_snapshot(&snapshot))
        return;

    snprintf(g_dlna_protocol_state.transport_state, sizeof(g_dlna_protocol_state.transport_state), "%s",
             dlna_protocol_transport_state_from_player_state(snapshot.state));
    snprintf(g_dlna_protocol_state.transport_status, sizeof(g_dlna_protocol_state.transport_status), "%s",
             dlna_protocol_transport_status_from_player_state(snapshot.state));
    dlna_protocol_state_set_transport_timing(snapshot.duration_ms, snapshot.position_ms);
    g_dlna_protocol_state.volume = snapshot.volume;
    g_dlna_protocol_state.mute = snapshot.mute;
}

void dlna_protocol_state_on_player_event(const PlayerEvent *event)
{
    if (!event)
        return;

    switch (event->type)
    {
    case PLAYER_EVENT_STATE_CHANGED:
        snprintf(g_dlna_protocol_state.transport_state, sizeof(g_dlna_protocol_state.transport_state), "%s",
                 dlna_protocol_transport_state_from_player_state(event->state));
        snprintf(g_dlna_protocol_state.transport_status, sizeof(g_dlna_protocol_state.transport_status), "%s",
                 dlna_protocol_transport_status_from_player_state(event->state));
        break;
    case PLAYER_EVENT_POSITION_CHANGED:
        dlna_protocol_format_hhmmss_from_ms(event->position_ms,
                                            g_dlna_protocol_state.transport_rel_time,
                                            sizeof(g_dlna_protocol_state.transport_rel_time));
        dlna_protocol_format_hhmmss_from_ms(event->position_ms,
                                            g_dlna_protocol_state.transport_abs_time,
                                            sizeof(g_dlna_protocol_state.transport_abs_time));
        break;
    case PLAYER_EVENT_DURATION_CHANGED:
        dlna_protocol_format_hhmmss_from_ms(event->duration_ms,
                                            g_dlna_protocol_state.transport_duration,
                                            sizeof(g_dlna_protocol_state.transport_duration));
        break;
    case PLAYER_EVENT_VOLUME_CHANGED:
        g_dlna_protocol_state.volume = event->volume;
        break;
    case PLAYER_EVENT_MUTE_CHANGED:
        g_dlna_protocol_state.mute = event->mute;
        break;
    case PLAYER_EVENT_URI_CHANGED:
        if (event->uri)
            snprintf(g_dlna_protocol_state.transport_uri, sizeof(g_dlna_protocol_state.transport_uri), "%s", event->uri);
        break;
    case PLAYER_EVENT_ERROR:
        dlna_protocol_state_set_transport_status("ERROR_OCCURRED");
        break;
    default:
        break;
    }
}
