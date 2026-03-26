#include "../handler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../handler_internal.h"
#include "player/player.h"

static bool is_hhmmss(const char *value)
{
    if (!value || strlen(value) != 8)
        return false;

    for (size_t i = 0; i < 8; ++i)
    {
        if (i == 2 || i == 5)
        {
            if (value[i] != ':')
                return false;
            continue;
        }
        if (!isdigit((unsigned char)value[i]))
            return false;
    }
    return true;
}

static const char *transport_state_from_player_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
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

    if (!value || !out_ms)
        return false;
    if (!is_hhmmss(value))
        return false;

    if (sscanf(value, "%d:%d:%d", &hour, &minute, &second) != 3)
        return false;
    if (hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    *out_ms = (hour * 3600 + minute * 60 + second) * 1000;
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

    if (strcmp(g_soap_runtime_state.transport_state, "PLAYING") != 0)
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_pause())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

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

    if (!player_stop())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_get_transport_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    PlayerState player_state = player_get_state();
    const char *transport_state = transport_state_from_player_state(player_state);
    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "%s", transport_state);

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

    int duration_ms = player_get_duration_ms();
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

    int position_ms = player_get_position_ms();
    int duration_ms = player_get_duration_ms();
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

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Unit", unit, sizeof(unit)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Target", target, sizeof(target)))
        return false;

    if (strcmp(unit, "REL_TIME") != 0)
    {
        soap_handler_set_fault(out, 710, "Seek mode not supported");
        return false;
    }

    if (!is_hhmmss(target))
    {
        soap_handler_set_fault(out, 402, "Invalid Args");
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
