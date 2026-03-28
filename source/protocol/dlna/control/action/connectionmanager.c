#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "../handler_internal.h"

bool connectionmanager_get_protocol_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;

    if (!out)
        return false;

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<Source>%s</Source>"
                       "<Sink>%s</Sink>",
                       g_soap_runtime_state.source_protocol_info,
                       g_soap_runtime_state.sink_protocol_info);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool connectionmanager_get_current_connection_ids(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;

    if (!out)
        return false;

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<ConnectionIDs>%s</ConnectionIDs>",
                       g_soap_runtime_state.connection_ids);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool connectionmanager_get_current_connection_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char connection_id[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "ConnectionID", connection_id, sizeof(connection_id)))
        return false;

    long requested_id = strtol(connection_id, NULL, 10);
    if (requested_id != 0)
    {
        soap_handler_set_fault(out, 706, "No Such Connection");
        return false;
    }

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<RcsID>0</RcsID>"
                       "<AVTransportID>0</AVTransportID>"
                       "<ProtocolInfo>%s</ProtocolInfo>"
                       "<PeerConnectionManager></PeerConnectionManager>"
                       "<PeerConnectionID>-1</PeerConnectionID>"
                       "<Direction>Input</Direction>"
                       "<Status>OK</Status>",
                       g_soap_runtime_state.sink_protocol_info);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}
