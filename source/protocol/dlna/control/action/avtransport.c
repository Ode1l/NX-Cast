#include "../handler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../handler_internal.h"

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

    snprintf(g_soap_runtime_state.transport_uri, sizeof(g_soap_runtime_state.transport_uri), "%s", uri);
    snprintf(g_soap_runtime_state.transport_uri_metadata, sizeof(g_soap_runtime_state.transport_uri_metadata), "%s", metadata);
    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "STOPPED");
    snprintf(g_soap_runtime_state.transport_rel_time, sizeof(g_soap_runtime_state.transport_rel_time), "00:00:00");
    snprintf(g_soap_runtime_state.transport_abs_time, sizeof(g_soap_runtime_state.transport_abs_time), "00:00:00");

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

    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "PLAYING");
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

    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "PAUSED_PLAYBACK");
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

    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "STOPPED");
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

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<CurrentTransportState>%s</CurrentTransportState>"
                       "<CurrentTransportStatus>%s</CurrentTransportStatus>"
                       "<CurrentSpeed>%s</CurrentSpeed>",
                       g_soap_runtime_state.transport_state,
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
                       g_soap_runtime_state.transport_duration,
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
                       g_soap_runtime_state.transport_duration,
                       g_soap_runtime_state.transport_uri,
                       g_soap_runtime_state.transport_rel_time,
                       g_soap_runtime_state.transport_abs_time);
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

    snprintf(g_soap_runtime_state.transport_rel_time, sizeof(g_soap_runtime_state.transport_rel_time), "%s", target);
    snprintf(g_soap_runtime_state.transport_abs_time, sizeof(g_soap_runtime_state.transport_abs_time), "%s", target);
    soap_handler_set_success(out, "");
    return true;
}
