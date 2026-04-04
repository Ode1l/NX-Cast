#include "soap_writer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t SOAP_WRITER_INITIAL_CAP = 512;

static bool soap_writer_reserve(SoapActionOutput *out, size_t extra)
{
    if (!out)
        return false;

    size_t needed = out->output_len + extra + 1;
    if (needed <= out->output_cap)
        return true;

    size_t new_cap = out->output_cap > 0 ? out->output_cap : SOAP_WRITER_INITIAL_CAP;
    while (new_cap < needed)
    {
        if (new_cap >= SOAP_HANDLER_OUTPUT_MAX)
            break;
        new_cap *= 2;
        if (new_cap > SOAP_HANDLER_OUTPUT_MAX)
            new_cap = SOAP_HANDLER_OUTPUT_MAX;
    }

    if (new_cap < needed)
        return false;

    char *new_buf = realloc(out->output_xml, new_cap);
    if (!new_buf)
        return false;

    out->output_xml = new_buf;
    out->output_cap = new_cap;
    if (out->output_len == 0)
        out->output_xml[0] = '\0';
    return true;
}

void soap_writer_clear(SoapActionOutput *out)
{
    if (!out)
        return;

    out->output_len = 0;
    if (out->output_xml && out->output_cap > 0)
        out->output_xml[0] = '\0';
}

void soap_writer_dispose(SoapActionOutput *out)
{
    if (!out)
        return;

    free(out->output_xml);
    out->output_xml = NULL;
    out->output_len = 0;
    out->output_cap = 0;
}

bool soap_writer_append_len(SoapActionOutput *out, const char *text, size_t len)
{
    if (!out || (!text && len > 0))
        return false;

    if (!soap_writer_reserve(out, len))
        return false;

    if (len > 0)
        memcpy(out->output_xml + out->output_len, text, len);
    out->output_len += len;
    out->output_xml[out->output_len] = '\0';
    return true;
}

bool soap_writer_append_raw(SoapActionOutput *out, const char *text)
{
    if (!text)
        return true;
    return soap_writer_append_len(out, text, strlen(text));
}

bool soap_writer_appendf(SoapActionOutput *out, const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;

    if (!out || !fmt)
        return false;

    va_start(args, fmt);
    va_copy(args_copy, args);
    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0)
    {
        va_end(args);
        return false;
    }

    if (!soap_writer_reserve(out, (size_t)needed))
    {
        va_end(args);
        return false;
    }

    int written = vsnprintf(out->output_xml + out->output_len,
                            out->output_cap - out->output_len,
                            fmt,
                            args);
    va_end(args);
    if (written < 0 || written != needed)
        return false;

    out->output_len += (size_t)written;
    return true;
}

bool soap_writer_append_escaped(SoapActionOutput *out, const char *text)
{
    if (!out)
        return false;

    const char *value = text ? text : "";
    size_t worst_case = strlen(value) * 6;
    if (!soap_writer_reserve(out, worst_case))
        return false;

    while (*value)
    {
        const char *replacement = NULL;
        char literal[2] = {0, 0};

        switch (*value)
        {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        default:
            literal[0] = *value;
            replacement = literal;
            break;
        }

        if (!soap_writer_append_raw(out, replacement))
            return false;
        ++value;
    }

    return true;
}

bool soap_writer_open_tag(SoapActionOutput *out, const char *tag)
{
    return soap_writer_appendf(out, "<%s>", tag);
}

bool soap_writer_close_tag(SoapActionOutput *out, const char *tag)
{
    return soap_writer_appendf(out, "</%s>", tag);
}

bool soap_writer_element_text(SoapActionOutput *out, const char *tag, const char *text)
{
    return soap_writer_open_tag(out, tag) &&
           soap_writer_append_escaped(out, text) &&
           soap_writer_close_tag(out, tag);
}

bool soap_writer_element_raw(SoapActionOutput *out, const char *tag, const char *raw_text)
{
    return soap_writer_open_tag(out, tag) &&
           soap_writer_append_raw(out, raw_text ? raw_text : "") &&
           soap_writer_close_tag(out, tag);
}

bool soap_writer_element_int(SoapActionOutput *out, const char *tag, long value)
{
    return soap_writer_open_tag(out, tag) &&
           soap_writer_appendf(out, "%ld", value) &&
           soap_writer_close_tag(out, tag);
}
