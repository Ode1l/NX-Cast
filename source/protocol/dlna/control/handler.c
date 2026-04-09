#include "handler.h"

#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "event_server.h"
#include "handler_internal.h"
#include "log/log.h"
#include "player/renderer.h"

char *soap_handler_strdup_printf(const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;
    char *buffer;

    if (!fmt)
        return NULL;

    va_start(args, fmt);
    va_copy(args_copy, args);
    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0)
    {
        va_end(args);
        return NULL;
    }

    buffer = malloc((size_t)needed + 1);
    if (!buffer)
    {
        va_end(args);
        return NULL;
    }

    vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buffer;
}

char *soap_handler_clip_for_log_alloc(const char *input, size_t max_len)
{
    size_t len;
    size_t copy_len;
    char *output;

    if (!input)
        return strdup("");

    len = strlen(input);
    if (len <= max_len)
        return strdup(input);

    if (max_len <= 3)
        return strdup("");

    copy_len = max_len - 3;
    output = malloc(max_len + 1);
    if (!output)
        return NULL;

    memcpy(output, input, copy_len);
    output[copy_len] = '.';
    output[copy_len + 1] = '.';
    output[copy_len + 2] = '.';
    output[copy_len + 3] = '\0';
    return output;
}

static void xml_decode_in_place(char *value)
{
    char *src;
    char *dst;

    if (!value || value[0] == '\0')
        return;

    src = value;
    dst = value;
    while (*src)
    {
        if (*src == '&')
        {
            if (strncmp(src, "&amp;", 5) == 0)
            {
                *dst++ = '&';
                src += 5;
                continue;
            }
            if (strncmp(src, "&lt;", 4) == 0)
            {
                *dst++ = '<';
                src += 4;
                continue;
            }
            if (strncmp(src, "&gt;", 4) == 0)
            {
                *dst++ = '>';
                src += 4;
                continue;
            }
            if (strncmp(src, "&quot;", 6) == 0)
            {
                *dst++ = '"';
                src += 6;
                continue;
            }
            if (strncmp(src, "&apos;", 6) == 0)
            {
                *dst++ = '\'';
                src += 6;
                continue;
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

static void trim_whitespace_in_place(char *value)
{
    size_t len;
    size_t start = 0;

    if (!value)
        return;

    len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1]))
        value[--len] = '\0';

    while (value[start] && isspace((unsigned char)value[start]))
        ++start;

    if (start > 0)
        memmove(value, value + start, strlen(value + start) + 1);
}

bool soap_handler_xml_escape(const char *value, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;

    out[0] = '\0';
    if (!value)
        return true;

    size_t used = 0;
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

        size_t replacement_len = strlen(replacement);
        if (used + replacement_len >= out_size)
            return false;

        memcpy(out + used, replacement, replacement_len);
        used += replacement_len;
        ++value;
    }

    out[used] = '\0';
    return true;
}

char *soap_handler_xml_escape_alloc(const char *value)
{
    const char *input = value ? value : "";
    size_t input_len = strlen(input);
    size_t out_size = input_len * 6 + 1;
    char *escaped = malloc(out_size);

    if (!escaped)
        return NULL;

    if (!soap_handler_xml_escape(input, escaped, out_size))
    {
        free(escaped);
        return NULL;
    }

    return escaped;
}

static void soap_handler_on_renderer_event(const RendererEvent *event, void *user)
{
    (void)user;

    if (!event)
        return;
    dlna_protocol_state_on_renderer_event(event);
    event_server_on_renderer_event(event);
}

void soap_handler_set_fault(SoapActionOutput *out, int code, const char *description)
{
    if (!out)
        return;

    out->success = false;
    out->fault_code = code;
    out->fault_description = description;
    soap_writer_clear(out);
}

void soap_handler_set_success(SoapActionOutput *out, const char *xml)
{
    if (!out)
        return;

    out->success = true;
    out->fault_code = 0;
    out->fault_description = NULL;

    if (!xml)
        return;

    if (xml == out->output_xml)
        return;

    soap_writer_clear(out);
    if (!soap_writer_append_raw(out, xml))
    {
        out->success = false;
        out->fault_code = 501;
        out->fault_description = "Action Failed";
        soap_writer_clear(out);
    }
}

bool soap_handler_extract_xml_value_alloc(const char *xml, const char *tag, char **out)
{
    if (!xml || !tag || !out)
        return false;

    *out = NULL;

    size_t tag_len = strlen(tag);
    const char *cursor = xml;

    while (*cursor)
    {
        const char *open = strchr(cursor, '<');
        if (!open)
            break;

        if (open[1] == '/' || open[1] == '?' || open[1] == '!')
        {
            cursor = open + 1;
            continue;
        }

        const char *name_start = open + 1;
        while (*name_start && isspace((unsigned char)*name_start))
            ++name_start;

        const char *name_end = name_start;
        while (*name_end && !isspace((unsigned char)*name_end) && *name_end != '>' && *name_end != '/')
            ++name_end;
        if (name_end == name_start)
        {
            cursor = open + 1;
            continue;
        }

        const char *local_start = memchr(name_start, ':', (size_t)(name_end - name_start));
        if (local_start)
            ++local_start;
        else
            local_start = name_start;
        size_t local_len = (size_t)(name_end - local_start);
        if (local_len != tag_len || strncasecmp(local_start, tag, tag_len) != 0)
        {
            cursor = name_end;
            continue;
        }

        const char *open_end = strchr(name_end, '>');
        if (!open_end)
            return false;
        if (open_end > open && open_end[-1] == '/')
        {
            cursor = open_end + 1;
            continue;
        }

        const char *value_start = open_end + 1;
        const char *scan = value_start;
        while (true)
        {
            const char *close = strstr(scan, "</");
            if (!close)
                break;

            const char *close_name_start = close + 2;
            while (*close_name_start && isspace((unsigned char)*close_name_start))
                ++close_name_start;
            const char *close_name_end = close_name_start;
            while (*close_name_end && !isspace((unsigned char)*close_name_end) && *close_name_end != '>')
                ++close_name_end;
            if (close_name_end == close_name_start)
            {
                scan = close + 2;
                continue;
            }

            const char *close_local_start = memchr(close_name_start, ':', (size_t)(close_name_end - close_name_start));
            if (close_local_start)
                ++close_local_start;
            else
                close_local_start = close_name_start;
            size_t close_local_len = (size_t)(close_name_end - close_local_start);
            if (close_local_len == tag_len && strncasecmp(close_local_start, tag, tag_len) == 0)
            {
                size_t value_len = (size_t)(close - value_start);
                char *value = malloc(value_len + 1);
                if (!value)
                    return false;

                memcpy(value, value_start, value_len);
                value[value_len] = '\0';
                xml_decode_in_place(value);
                trim_whitespace_in_place(value);
                *out = value;
                return true;
            }

            scan = close + 2;
        }

        cursor = open_end + 1;
    }

    return false;
}

bool soap_handler_require_arg_alloc(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                                    char **value_out)
{
    char *body_short = NULL;
    char *clipped = NULL;
    size_t body_len;

    if (!ctx || !out || !arg_name || !value_out)
        return false;

    *value_out = NULL;
    if (!soap_handler_extract_xml_value_alloc(ctx->body, arg_name, value_out))
    {
        body_len = ctx->body ? strlen(ctx->body) : 0;
        body_short = soap_handler_clip_for_log_alloc(ctx->body ? ctx->body : "", 191);
        log_warn("[soap-arg] missing required arg service=%s action=%s arg=%s\n",
                 ctx->service_name ? ctx->service_name : "(null)",
                 ctx->action_name ? ctx->action_name : "(null)",
                 arg_name);
        log_warn("[soap-arg] request body_bytes=%zu body=%s\n", body_len,
                 body_len > 0 ? (body_short ? body_short : "<oom>") : "<empty>");
        free(body_short);
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    clipped = soap_handler_clip_for_log_alloc(*value_out, 95);
    log_debug("[soap-arg] required arg service=%s action=%s arg=%s value=%s\n",
              ctx->service_name ? ctx->service_name : "(null)",
              ctx->action_name ? ctx->action_name : "(null)",
              arg_name,
              clipped ? clipped : "<oom>");
    free(clipped);
    return true;
}

bool soap_handler_try_arg_alloc(const SoapActionContext *ctx, const char *arg_name, char **value_out)
{
    char *clipped = NULL;

    if (!ctx || !arg_name || !value_out)
        return false;

    *value_out = NULL;
    bool ok = soap_handler_extract_xml_value_alloc(ctx->body, arg_name, value_out);
    if (!ok)
    {
        log_debug("[soap-arg] optional arg missing service=%s action=%s arg=%s\n",
                  ctx->service_name ? ctx->service_name : "(null)",
                  ctx->action_name ? ctx->action_name : "(null)",
                  arg_name);
        return false;
    }

    clipped = soap_handler_clip_for_log_alloc(*value_out, 95);
    log_debug("[soap-arg] optional arg service=%s action=%s arg=%s value=%s\n",
              ctx->service_name ? ctx->service_name : "(null)",
              ctx->action_name ? ctx->action_name : "(null)",
              arg_name,
              clipped ? clipped : "<oom>");
    free(clipped);
    return true;
}

bool soap_handler_try_http_header_alloc(const SoapActionContext *ctx, const char *header_name, char **value_out)
{
    size_t header_len;
    const char *cursor;

    if (!ctx || !ctx->request || !header_name || !value_out)
        return false;

    *value_out = NULL;
    header_len = strlen(header_name);
    cursor = ctx->request;

    while (*cursor)
    {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (line_len == 0)
            break;

        if (line_len > header_len + 1 &&
            strncasecmp(cursor, header_name, header_len) == 0 &&
            cursor[header_len] == ':')
        {
            const char *value_start = cursor + header_len + 1;
            size_t copy_len;
            char *value;

            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;

            copy_len = line_end ? (size_t)(line_end - value_start) : strlen(value_start);
            value = malloc(copy_len + 1);
            if (!value)
                return false;

            memcpy(value, value_start, copy_len);
            value[copy_len] = '\0';
            trim_whitespace_in_place(value);
            if (value[0] == '\0')
            {
                free(value);
                return false;
            }

            *value_out = value;
            return true;
        }

        if (!line_end)
            break;
        cursor = line_end + 2;
    }

    return false;
}

void soap_handler_init(void)
{
    dlna_protocol_state_init();

    renderer_set_event_callback(soap_handler_on_renderer_event, NULL);
    if (!renderer_init())
    {
        log_warn("[soap-handler] renderer init failed, actions may not work.\n");
        return;
    }
    dlna_protocol_state_sync_from_renderer();
}

void soap_handler_shutdown(void)
{
    log_info("[soap-handler] shutdown begin\n");
    renderer_set_event_callback(NULL, NULL);
    log_info("[soap-handler] shutdown step=renderer_deinit begin\n");
    renderer_deinit();
    log_info("[soap-handler] shutdown step=renderer_deinit done\n");
    dlna_protocol_state_reset();
    log_info("[soap-handler] shutdown step=protocol_state_reset done\n");
}
