#include "handler.h"

#include <stdio.h>
#include <string.h>

#include "handler_internal.h"

SoapRuntimeState g_soap_runtime_state;

void soap_handler_set_fault(SoapActionOutput *out, int code, const char *description)
{
    if (!out)
        return;

    out->success = false;
    out->fault_code = code;
    out->fault_description = description;
    out->output_xml[0] = '\0';
}

void soap_handler_set_success(SoapActionOutput *out, const char *xml)
{
    if (!out)
        return;

    out->success = true;
    out->fault_code = 0;
    out->fault_description = NULL;

    if (!xml)
    {
        out->output_xml[0] = '\0';
        return;
    }

    snprintf(out->output_xml, sizeof(out->output_xml), "%s", xml);
}

bool soap_handler_extract_xml_value(const char *xml, const char *tag, char *out, size_t out_size)
{
    if (!xml || !tag || !out || out_size == 0)
        return false;

    char open_tag[64];
    char close_tag[64];
    int open_len = snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    int close_len = snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
    if (open_len <= 0 || close_len <= 0)
        return false;

    const char *start = strstr(xml, open_tag);
    if (!start)
        return false;

    start += (size_t)open_len;
    const char *end = strstr(start, close_tag);
    if (!end)
        return false;

    size_t value_len = (size_t)(end - start);
    if (value_len >= out_size)
        value_len = out_size - 1;

    memcpy(out, start, value_len);
    out[value_len] = '\0';
    return true;
}

bool soap_handler_require_arg(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                              char *buf, size_t buf_size)
{
    if (!ctx || !out || !arg_name || !buf || buf_size == 0)
        return false;

    if (!soap_handler_extract_xml_value(ctx->body, arg_name, buf, buf_size))
    {
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }
    return true;
}

void soap_handler_init(void)
{
    memset(&g_soap_runtime_state, 0, sizeof(g_soap_runtime_state));
    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "STOPPED");
    snprintf(g_soap_runtime_state.transport_status, sizeof(g_soap_runtime_state.transport_status), "OK");
    snprintf(g_soap_runtime_state.transport_speed, sizeof(g_soap_runtime_state.transport_speed), "1");
    snprintf(g_soap_runtime_state.transport_duration, sizeof(g_soap_runtime_state.transport_duration), "00:00:00");
    snprintf(g_soap_runtime_state.transport_rel_time, sizeof(g_soap_runtime_state.transport_rel_time), "00:00:00");
    snprintf(g_soap_runtime_state.transport_abs_time, sizeof(g_soap_runtime_state.transport_abs_time), "00:00:00");
    g_soap_runtime_state.volume = 20;
    g_soap_runtime_state.mute = false;
    g_soap_runtime_state.source_protocol_info[0] = '\0';
    snprintf(g_soap_runtime_state.sink_protocol_info, sizeof(g_soap_runtime_state.sink_protocol_info),
             "http-get:*:audio/mpeg:*,http-get:*:audio/mp4:*,http-get:*:video/mp4:*");
    snprintf(g_soap_runtime_state.connection_ids, sizeof(g_soap_runtime_state.connection_ids), "0");
    g_soap_runtime_state.initialized = true;
}

void soap_handler_shutdown(void)
{
    memset(&g_soap_runtime_state, 0, sizeof(g_soap_runtime_state));
}
