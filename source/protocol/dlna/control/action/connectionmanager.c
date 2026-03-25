#include "../handler.h"

#include <stdio.h>

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
