#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"
#include "protocol_state.h"
#include "soap_writer.h"

void soap_handler_set_fault(SoapActionOutput *out, int code, const char *description);
void soap_handler_set_success(SoapActionOutput *out, const char *xml);

bool soap_handler_xml_escape(const char *value, char *out, size_t out_size);
char *soap_handler_strdup_printf(const char *fmt, ...);
char *soap_handler_clip_for_log_alloc(const char *input, size_t max_len);
char *soap_handler_xml_escape_alloc(const char *value);
bool soap_handler_extract_xml_value_alloc(const char *xml, const char *tag, char **out);
bool soap_handler_require_arg_alloc(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                                    char **value_out);
bool soap_handler_try_arg_alloc(const SoapActionContext *ctx, const char *arg_name, char **value_out);
bool soap_handler_try_http_header_alloc(const SoapActionContext *ctx, const char *header_name, char **value_out);
