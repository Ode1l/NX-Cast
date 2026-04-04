#include "handler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "event_server.h"
#include "handler_internal.h"
#include "log/log.h"
#include "player/player.h"

SoapRuntimeState g_soap_runtime_state;

static void clip_for_log(const char *input, char *output, size_t output_size)
{
    if (!output || output_size == 0)
        return;
    if (!input)
    {
        output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    if (len + 1 <= output_size)
    {
        snprintf(output, output_size, "%s", input);
        return;
    }

    if (output_size <= 4)
    {
        output[0] = '\0';
        return;
    }

    size_t copy_len = output_size - 4;
    memcpy(output, input, copy_len);
    output[copy_len] = '.';
    output[copy_len + 1] = '.';
    output[copy_len + 2] = '.';
    output[copy_len + 3] = '\0';
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

static const char *transport_state_from_player_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_IDLE:
        return "NO_MEDIA_PRESENT";
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return "TRANSITIONING";
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
}

static const char *transport_status_from_player_state(PlayerState state)
{
    return state == PLAYER_STATE_ERROR ? "ERROR_OCCURRED" : "OK";
}

static void format_hhmmss_from_ms(int value_ms, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;

    if (value_ms < 0)
        value_ms = 0;

    int total_seconds = value_ms / 1000;
    int hour = total_seconds / 3600;
    int minute = (total_seconds % 3600) / 60;
    int second = total_seconds % 60;
    snprintf(out, out_size, "%02d:%02d:%02d", hour, minute, second);
}

static void soap_handler_on_player_event(const PlayerEvent *event, void *user)
{
    (void)user;

    if (!event)
        return;

    switch (event->type)
    {
    case PLAYER_EVENT_STATE_CHANGED:
        snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "%s",
                 transport_state_from_player_state(event->state));
        snprintf(g_soap_runtime_state.transport_status, sizeof(g_soap_runtime_state.transport_status), "%s",
                 transport_status_from_player_state(event->state));
        break;
    case PLAYER_EVENT_POSITION_CHANGED:
        format_hhmmss_from_ms(event->position_ms, g_soap_runtime_state.transport_rel_time,
                              sizeof(g_soap_runtime_state.transport_rel_time));
        format_hhmmss_from_ms(event->position_ms, g_soap_runtime_state.transport_abs_time,
                              sizeof(g_soap_runtime_state.transport_abs_time));
        break;
    case PLAYER_EVENT_DURATION_CHANGED:
        format_hhmmss_from_ms(event->duration_ms, g_soap_runtime_state.transport_duration,
                              sizeof(g_soap_runtime_state.transport_duration));
        break;
    case PLAYER_EVENT_VOLUME_CHANGED:
        g_soap_runtime_state.volume = event->volume;
        break;
    case PLAYER_EVENT_MUTE_CHANGED:
        g_soap_runtime_state.mute = event->mute;
        break;
    case PLAYER_EVENT_URI_CHANGED:
        if (event->uri)
            snprintf(g_soap_runtime_state.transport_uri, sizeof(g_soap_runtime_state.transport_uri), "%s", event->uri);
        break;
    case PLAYER_EVENT_ERROR:
        snprintf(g_soap_runtime_state.transport_status, sizeof(g_soap_runtime_state.transport_status), "ERROR_OCCURRED");
        break;
    default:
        break;
    }

    event_server_on_player_event(event);
}

static void sync_runtime_state_from_player_snapshot(void)
{
    PlayerSnapshot snapshot;

    if (!player_get_snapshot(&snapshot))
        return;

    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "%s",
             transport_state_from_player_state(snapshot.state));
    snprintf(g_soap_runtime_state.transport_status, sizeof(g_soap_runtime_state.transport_status), "%s",
             transport_status_from_player_state(snapshot.state));
    format_hhmmss_from_ms(snapshot.duration_ms, g_soap_runtime_state.transport_duration,
                          sizeof(g_soap_runtime_state.transport_duration));
    format_hhmmss_from_ms(snapshot.position_ms, g_soap_runtime_state.transport_rel_time,
                          sizeof(g_soap_runtime_state.transport_rel_time));
    format_hhmmss_from_ms(snapshot.position_ms, g_soap_runtime_state.transport_abs_time,
                          sizeof(g_soap_runtime_state.transport_abs_time));
    g_soap_runtime_state.volume = snapshot.volume;
    g_soap_runtime_state.mute = snapshot.mute;
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

bool soap_handler_extract_xml_value(const char *xml, const char *tag, char *out, size_t out_size)
{
    if (!xml || !tag || !out || out_size == 0)
        return false;

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
                if (value_len >= out_size)
                    value_len = out_size - 1;
                memcpy(out, value_start, value_len);
                out[value_len] = '\0';
                xml_decode_in_place(out);

                size_t begin = 0;
                while (out[begin] && isspace((unsigned char)out[begin]))
                    ++begin;
                if (begin > 0)
                    memmove(out, out + begin, strlen(out + begin) + 1);

                size_t end = strlen(out);
                while (end > 0 && isspace((unsigned char)out[end - 1]))
                    out[--end] = '\0';
                return true;
            }

            scan = close + 2;
        }

        cursor = open_end + 1;
    }

    return false;
}

bool soap_handler_require_arg(const SoapActionContext *ctx, SoapActionOutput *out, const char *arg_name,
                              char *buf, size_t buf_size)
{
    if (!ctx || !out || !arg_name || !buf || buf_size == 0)
        return false;

    if (!soap_handler_extract_xml_value(ctx->body, arg_name, buf, buf_size))
    {
        char body_short[192];
        size_t body_len = ctx->body ? strlen(ctx->body) : 0;
        clip_for_log(ctx->body ? ctx->body : "", body_short, sizeof(body_short));
        log_warn("[soap-arg] missing required arg service=%s action=%s arg=%s\n",
                 ctx->service_name ? ctx->service_name : "(null)",
                 ctx->action_name ? ctx->action_name : "(null)",
                 arg_name);
        log_warn("[soap-arg] request body_bytes=%zu body=%s\n", body_len,
                 body_len > 0 ? body_short : "<empty>");
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    char clipped[96];
    clip_for_log(buf, clipped, sizeof(clipped));
    log_debug("[soap-arg] required arg service=%s action=%s arg=%s value=%s\n",
              ctx->service_name ? ctx->service_name : "(null)",
              ctx->action_name ? ctx->action_name : "(null)",
              arg_name,
              clipped);
    return true;
}

bool soap_handler_try_arg(const SoapActionContext *ctx, const char *arg_name, char *buf, size_t buf_size)
{
    if (!ctx || !arg_name || !buf || buf_size == 0)
        return false;

    bool ok = soap_handler_extract_xml_value(ctx->body, arg_name, buf, buf_size);
    if (!ok)
    {
        log_debug("[soap-arg] optional arg missing service=%s action=%s arg=%s\n",
                  ctx->service_name ? ctx->service_name : "(null)",
                  ctx->action_name ? ctx->action_name : "(null)",
                  arg_name);
        return false;
    }

    char clipped[96];
    clip_for_log(buf, clipped, sizeof(clipped));
    log_debug("[soap-arg] optional arg service=%s action=%s arg=%s value=%s\n",
              ctx->service_name ? ctx->service_name : "(null)",
              ctx->action_name ? ctx->action_name : "(null)",
              arg_name,
              clipped);
    return true;
}

bool soap_handler_try_http_header(const SoapActionContext *ctx, const char *header_name, char *buf, size_t buf_size)
{
    size_t header_len;
    const char *cursor;

    if (!ctx || !ctx->request || !header_name || !buf || buf_size == 0)
        return false;

    buf[0] = '\0';
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

            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;

            copy_len = line_end ? (size_t)(line_end - value_start) : strlen(value_start);
            if (copy_len >= buf_size)
                copy_len = buf_size - 1;

            memcpy(buf, value_start, copy_len);
            buf[copy_len] = '\0';
            trim_whitespace_in_place(buf);
            return buf[0] != '\0';
        }

        if (!line_end)
            break;
        cursor = line_end + 2;
    }

    return false;
}

void soap_handler_init(void)
{
    memset(&g_soap_runtime_state, 0, sizeof(g_soap_runtime_state));
    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "NO_MEDIA_PRESENT");
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

    player_set_event_callback(soap_handler_on_player_event, NULL);
    if (!player_init())
    {
        log_warn("[soap-handler] player init failed, actions may not work.\n");
        return;
    }
    sync_runtime_state_from_player_snapshot();
}

void soap_handler_shutdown(void)
{
    player_set_event_callback(NULL, NULL);
    player_deinit();
    memset(&g_soap_runtime_state, 0, sizeof(g_soap_runtime_state));
}
