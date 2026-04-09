#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../handler_internal.h"

static char *connectionmanager_escape_dup(const char *value)
{
    return soap_handler_xml_escape_alloc(value);
}

bool connectionmanager_get_protocol_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!out)
        return false;

    char *escaped_source = connectionmanager_escape_dup(state->source_protocol_info);
    char *escaped_sink = connectionmanager_escape_dup(state->sink_protocol_info);
    if (!escaped_source || !escaped_sink)
    {
        free(escaped_source);
        free(escaped_sink);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_raw(out, "Source", escaped_source) ||
        !soap_writer_element_raw(out, "Sink", escaped_sink))
    {
        free(escaped_source);
        free(escaped_sink);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(escaped_source);
    free(escaped_sink);
    soap_handler_set_success(out, NULL);
    return true;
}

bool connectionmanager_get_current_connection_ids(const SoapActionContext *ctx, SoapActionOutput *out)
{
    (void)ctx;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!out)
        return false;

    soap_writer_clear(out);
    if (!soap_writer_element_text(out, "ConnectionIDs", state->current_connection_ids))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}

bool connectionmanager_get_current_connection_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *connection_id = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "ConnectionID", &connection_id))
        return false;

    long requested_id = strtol(connection_id, NULL, 10);
    if (requested_id != 0)
    {
        free(connection_id);
        soap_handler_set_fault(out, 706, "No Such Connection");
        return false;
    }

    char *escaped_protocol_info = connectionmanager_escape_dup(state->sink_protocol_info);
    if (!escaped_protocol_info)
    {
        free(connection_id);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "RcsID", 0) ||
        !soap_writer_element_int(out, "AVTransportID", 0) ||
        !soap_writer_element_raw(out, "ProtocolInfo", escaped_protocol_info) ||
        !soap_writer_element_text(out, "PeerConnectionManager", "") ||
        !soap_writer_element_int(out, "PeerConnectionID", -1) ||
        !soap_writer_element_text(out, "Direction", state->a_arg_type_direction) ||
        !soap_writer_element_text(out, "Status", "OK"))
    {
        free(connection_id);
        free(escaped_protocol_info);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(connection_id);
    free(escaped_protocol_info);
    soap_handler_set_success(out, NULL);
    return true;
}
