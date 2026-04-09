#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"

void soap_writer_clear(SoapActionOutput *out);
void soap_writer_dispose(SoapActionOutput *out);

bool soap_writer_append_raw(SoapActionOutput *out, const char *text);
bool soap_writer_append_len(SoapActionOutput *out, const char *text, size_t len);
bool soap_writer_appendf(SoapActionOutput *out, const char *fmt, ...);
bool soap_writer_append_escaped(SoapActionOutput *out, const char *text);

bool soap_writer_open_tag(SoapActionOutput *out, const char *tag);
bool soap_writer_close_tag(SoapActionOutput *out, const char *tag);
bool soap_writer_element_text(SoapActionOutput *out, const char *tag, const char *text);
bool soap_writer_element_raw(SoapActionOutput *out, const char *tag, const char *raw_text);
bool soap_writer_element_int(SoapActionOutput *out, const char *tag, long value);
