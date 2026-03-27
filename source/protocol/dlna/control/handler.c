#include "handler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "handler_internal.h"
#include "log/log.h"
#include "player/player.h"

SoapRuntimeState g_soap_runtime_state;

static const char *transport_state_from_player_state(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
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
        snprintf(g_soap_runtime_state.transport_status, sizeof(g_soap_runtime_state.transport_status), "ERROR");
        break;
    default:
        break;
    }
}

static void sync_runtime_state_from_player_snapshot(void)
{
    snprintf(g_soap_runtime_state.transport_state, sizeof(g_soap_runtime_state.transport_state), "%s",
             transport_state_from_player_state(player_get_state()));
    format_hhmmss_from_ms(player_get_duration_ms(), g_soap_runtime_state.transport_duration,
                          sizeof(g_soap_runtime_state.transport_duration));
    format_hhmmss_from_ms(player_get_position_ms(), g_soap_runtime_state.transport_rel_time,
                          sizeof(g_soap_runtime_state.transport_rel_time));
    format_hhmmss_from_ms(player_get_position_ms(), g_soap_runtime_state.transport_abs_time,
                          sizeof(g_soap_runtime_state.transport_abs_time));
    g_soap_runtime_state.volume = player_get_volume();
    g_soap_runtime_state.mute = player_get_mute();
}

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
