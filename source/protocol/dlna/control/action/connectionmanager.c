#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "../handler_internal.h"

bool connectionmanager_get_protocol_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;

    if (!out)
        return false;

    char escaped_source[sizeof(g_soap_runtime_state.source_protocol_info) * 2];
    char escaped_sink[sizeof(g_soap_runtime_state.sink_protocol_info) * 2];
    if (!soap_handler_xml_escape(g_soap_runtime_state.source_protocol_info, escaped_source, sizeof(escaped_source)) ||
        !soap_handler_xml_escape(g_soap_runtime_state.sink_protocol_info, escaped_sink, sizeof(escaped_sink)))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_raw(out, "Source", escaped_source) ||
        !soap_writer_element_raw(out, "Sink", escaped_sink))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}

bool connectionmanager_get_current_connection_ids(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;

    if (!out)
        return false;

    soap_writer_clear(out);
    if (!soap_writer_element_text(out, "ConnectionIDs", g_soap_runtime_state.connection_ids))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
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

    char escaped_protocol_info[sizeof(g_soap_runtime_state.sink_protocol_info) * 2];
    if (!soap_handler_xml_escape(g_soap_runtime_state.sink_protocol_info,
                                 escaped_protocol_info,
                                 sizeof(escaped_protocol_info)))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "RcsID", 0) ||
        !soap_writer_element_int(out, "AVTransportID", 0) ||
        !soap_writer_element_raw(out, "ProtocolInfo", escaped_protocol_info) ||
        !soap_writer_element_text(out, "PeerConnectionManager", "") ||
        !soap_writer_element_int(out, "PeerConnectionID", -1) ||
        !soap_writer_element_text(out, "Direction", "Input") ||
        !soap_writer_element_text(out, "Status", "OK"))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}
