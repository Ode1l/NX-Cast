#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"
#include "soap_writer.h"

typedef struct
{
    char transport_uri[SOAP_TRANSPORT_URI_MAX];
    char transport_uri_metadata[SOAP_TRANSPORT_METADATA_MAX];
    char transport_state[32];
    char transport_status[16];
    char transport_speed[16];
    char transport_duration[16];
    char transport_rel_time[16];
    char transport_abs_time[16];
    int volume;
    bool mute;
    char source_protocol_info[256];
    char sink_protocol_info[512];
    char connection_ids[16];
    bool initialized;
} SoapRuntimeState;

extern SoapRuntimeState g_soap_runtime_state;

void soap_handler_set_fault(SoapActionOutput *out, int code, const char *description);
void soap_handler_set_success(SoapActionOutput *out, const char *xml);

bool soap_handler_extract_xml_value(const char *xml, const char *tag, char *out, size_t out_size);
bool soap_handler_xml_escape(const char *value, char *out, size_t out_size);
bool soap_handler_require_arg(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                              char *buf, size_t buf_size);
bool soap_handler_try_arg(const SoapActionContext *ctx, const char *arg_name, char *buf, size_t buf_size);
bool soap_handler_try_http_header(const SoapActionContext *ctx, const char *header_name, char *buf, size_t buf_size);
