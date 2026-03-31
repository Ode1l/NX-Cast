#include "../handler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../handler_internal.h"
#include "log/log.h"
#include "player/player.h"

static const char *transport_state_from_player_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return "TRANSITIONING";
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
}

static PlayerState sync_transport_state_from_player(void)
{
    PlayerSnapshot snapshot;
    PlayerState player_state = PLAYER_STATE_IDLE;

    if (player_get_snapshot(&snapshot))
        player_state = snapshot.state;
    else
        player_state = player_get_state();

    const char *transport_state = transport_state_from_player_state(player_state);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state);
    return player_state;
}

static void format_hhmmss_from_ms(int value_ms, char *out, size_t out_size)
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

static bool parse_hhmmss_to_ms(const char *value, int *out_ms)
{
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millis = 0;
    int parsed_chars = 0;
    const char *cursor = value;

    if (!value || !out_ms)
        return false;

    while (*cursor && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0')
        return false;

    if (sscanf(cursor, "%d:%d:%d%n", &hour, &minute, &second, &parsed_chars) != 3)
        return false;
    if (hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    if (cursor[parsed_chars] == '.')
    {
        const char *fraction = cursor + parsed_chars + 1;
        size_t frac_len = strlen(fraction);
        if (frac_len == 0 || frac_len > 3)
            return false;

        for (size_t i = 0; i < frac_len; ++i)
        {
            if (!isdigit((unsigned char)fraction[i]))
                return false;
            millis = millis * 10 + (fraction[i] - '0');
        }

        size_t pad_len = frac_len;
        while (pad_len < 3)
        {
            millis *= 10;
            ++pad_len;
        }
    }
    else if (cursor[parsed_chars] != '\0')
    {
        while (cursor[parsed_chars] && isspace((unsigned char)cursor[parsed_chars]))
            ++parsed_chars;
        if (cursor[parsed_chars] == '\0')
        {
            *out_ms = (hour * 3600 + minute * 60 + second) * 1000;
            return true;
        }
        return false;
    }

    *out_ms = (hour * 3600 + minute * 60 + second) * 1000 + millis;
    return true;
}

bool avtransport_set_uri(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char uri[sizeof(g_soap_runtime_state.transport_uri)];
    char metadata[sizeof(g_soap_runtime_state.transport_uri_metadata)];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "CurrentURI", uri, sizeof(uri)))
        return false;

    metadata[0] = '\0';
    soap_handler_extract_xml_value(ctx->body, "CurrentURIMetaData", metadata, sizeof(metadata));

    if (!player_set_uri(uri, metadata))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_uri, sizeof(g_soap_runtime_state.transport_uri), "%s", uri);
    snprintf(g_soap_runtime_state.transport_uri_metadata, sizeof(g_soap_runtime_state.transport_uri_metadata), "%s", metadata);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));

    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_play(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char speed[16];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Speed", speed, sizeof(speed)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_play())
    {
        soap_handler_set_fault(out, 704, "Playing failed");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_speed, sizeof(g_soap_runtime_state.transport_speed), "%s", speed);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_pause(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    PlayerState player_state = sync_transport_state_from_player();
    if (player_state == PLAYER_STATE_PAUSED)
    {
        soap_handler_set_success(out, "");
        return true;
    }
    if (player_state != PLAYER_STATE_PLAYING)
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_pause())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_stop(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    PlayerState player_state = sync_transport_state_from_player();
    if (player_state == PLAYER_STATE_STOPPED || player_state == PLAYER_STATE_IDLE)
    {
        soap_handler_set_success(out, "");
        return true;
    }

    if (!player_stop())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_get_transport_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    // Some mobile control points omit InstanceID for read-only queries.
    // Treat missing value as "0" for compatibility.
    if (!soap_handler_try_arg(ctx, "InstanceID", instance_id, sizeof(instance_id)))
    {
        snprintf(instance_id, sizeof(instance_id), "0");
        log_debug("[avtransport] default InstanceID=0 for GetTransportInfo\n");
    }

    PlayerState player_state = sync_transport_state_from_player();
    const char *transport_state = transport_state_from_player_state(player_state);

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<CurrentTransportState>%s</CurrentTransportState>"
                       "<CurrentTransportStatus>%s</CurrentTransportStatus>"
                       "<CurrentSpeed>%s</CurrentSpeed>",
                       transport_state,
                       g_soap_runtime_state.transport_status,
                       g_soap_runtime_state.transport_speed);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool avtransport_get_media_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    PlayerSnapshot snapshot;
    int duration_ms = 0;

    if (player_get_snapshot(&snapshot))
        duration_ms = snapshot.duration_ms;
    else
        duration_ms = player_get_duration_ms();

    char duration_str[16];
    format_hhmmss_from_ms(duration_ms, duration_str, sizeof(duration_str));
    snprintf(g_soap_runtime_state.transport_duration, sizeof(g_soap_runtime_state.transport_duration), "%s", duration_str);

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<NrTracks>1</NrTracks>"
                       "<MediaDuration>%s</MediaDuration>"
                       "<CurrentURI>%s</CurrentURI>"
                       "<CurrentURIMetaData></CurrentURIMetaData>"
                       "<NextURI></NextURI>"
                       "<NextURIMetaData></NextURIMetaData>"
                       "<PlayMedium>NETWORK</PlayMedium>"
                       "<RecordMedium>NOT_IMPLEMENTED</RecordMedium>"
                       "<WriteStatus>NOT_IMPLEMENTED</WriteStatus>",
                       duration_str,
                       g_soap_runtime_state.transport_uri);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool avtransport_get_position_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    PlayerSnapshot snapshot;
    int position_ms = 0;
    int duration_ms = 0;

    if (player_get_snapshot(&snapshot))
    {
        position_ms = snapshot.position_ms;
        duration_ms = snapshot.duration_ms;
    }
    else
    {
        position_ms = player_get_position_ms();
        duration_ms = player_get_duration_ms();
    }

    char duration_str[16];
    char rel_time_str[16];
    char abs_time_str[16];

    format_hhmmss_from_ms(duration_ms, duration_str, sizeof(duration_str));
    format_hhmmss_from_ms(position_ms, rel_time_str, sizeof(rel_time_str));
    format_hhmmss_from_ms(position_ms, abs_time_str, sizeof(abs_time_str));

    snprintf(g_soap_runtime_state.transport_duration, sizeof(g_soap_runtime_state.transport_duration), "%s", duration_str);
    snprintf(g_soap_runtime_state.transport_rel_time, sizeof(g_soap_runtime_state.transport_rel_time), "%s", rel_time_str);
    snprintf(g_soap_runtime_state.transport_abs_time, sizeof(g_soap_runtime_state.transport_abs_time), "%s", abs_time_str);

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<Track>1</Track>"
                       "<TrackDuration>%s</TrackDuration>"
                       "<TrackMetaData>NOT_IMPLEMENTED</TrackMetaData>"
                       "<TrackURI>%s</TrackURI>"
                       "<RelTime>%s</RelTime>"
                       "<AbsTime>%s</AbsTime>"
                       "<RelCount>0</RelCount>"
                       "<AbsCount>0</AbsCount>",
                       duration_str,
                       g_soap_runtime_state.transport_uri,
                       rel_time_str,
                       abs_time_str);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool avtransport_seek(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char unit[32];
    char target[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_try_arg(ctx, "InstanceID", instance_id, sizeof(instance_id)))
    {
        snprintf(instance_id, sizeof(instance_id), "0");
        log_debug("[avtransport] default InstanceID=0 for Seek\n");
    }

    if (!soap_handler_try_arg(ctx, "Unit", unit, sizeof(unit)))
    {
        snprintf(unit, sizeof(unit), "REL_TIME");
        log_debug("[avtransport] default Unit=REL_TIME for Seek\n");
    }

    if (!soap_handler_try_arg(ctx, "Target", target, sizeof(target)))
    {
        snprintf(target, sizeof(target), "00:00:00");
        log_debug("[avtransport] default Target=00:00:00 for Seek\n");
    }

    if (strcasecmp(unit, "REL_TIME") != 0 && strcasecmp(unit, "ABS_TIME") != 0)
    {
        soap_handler_set_fault(out, 710, "Seek mode not supported");
        return false;
    }

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    PlayerSnapshot snapshot;
    PlayerState player_state;
    bool seekable;

    if (player_get_snapshot(&snapshot))
    {
        player_state = snapshot.state;
        seekable = snapshot.seekable;
    }
    else
    {
        player_state = sync_transport_state_from_player();
        seekable = player_is_seekable();
    }

    if ((player_state != PLAYER_STATE_PLAYING && player_state != PLAYER_STATE_PAUSED) || !seekable)
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    int target_ms = 0;
    if (!parse_hhmmss_to_ms(target, &target_ms))
    {
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    if (!player_seek_ms(target_ms))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    soap_handler_set_success(out, "");
    return true;
}
