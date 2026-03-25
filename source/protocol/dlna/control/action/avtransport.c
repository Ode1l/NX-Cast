#include "../handler.h"

#include <stdio.h>
#include <string.h>

#include "../handler_internal.h"

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
