#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"
#include "protocol_state.h"
#include "soap_writer.h"

void soap_handler_set_fault(SoapActionOutput *out, int code, const char *description);
void soap_handler_set_success(SoapActionOutput *out, const char *xml);

bool soap_handler_extract_xml_value(const char *xml, const char *tag, char *out, size_t out_size);
bool soap_handler_xml_escape(const char *value, char *out, size_t out_size);
bool soap_handler_require_arg(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                              char *buf, size_t buf_size);
bool soap_handler_try_arg(const SoapActionContext *ctx, const char *arg_name, char *buf, size_t buf_size);
bool soap_handler_try_http_header(const SoapActionContext *ctx, const char *header_name, char *buf, size_t buf_size);
