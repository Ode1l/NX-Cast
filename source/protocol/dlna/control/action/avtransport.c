#include "../handler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../handler_internal.h"
#include "log/log.h"
#include "player/player.h"

typedef enum
{
    AVTRANSPORT_ACTION_PLAY = 1u << 0,
    AVTRANSPORT_ACTION_STOP = 1u << 1,
    AVTRANSPORT_ACTION_PAUSE = 1u << 2,
    AVTRANSPORT_ACTION_SEEK = 1u << 3
} AvTransportActionMask;

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

static PlayerState sync_transport_state_from_player(void)
{
    PlayerSnapshot snapshot;
    PlayerState player_state = PLAYER_STATE_IDLE;

    if (player_get_snapshot(&snapshot))
        player_state = snapshot.state;
    else
        player_state = player_get_state();

    const char *transport_state = transport_state_from_player_state(player_state);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state);
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             transport_status_from_player_state(player_state));
    return player_state;
}

static void avtransport_get_snapshot(PlayerSnapshot *snapshot)
{
    if (!snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    if (player_get_snapshot(snapshot))
        return;

    snapshot->state = player_get_state();
    snapshot->position_ms = player_get_position_ms();
    snapshot->duration_ms = player_get_duration_ms();
    snapshot->volume = player_get_volume();
    snapshot->mute = player_get_mute();
    snapshot->seekable = player_is_seekable();
    snapshot->has_media = g_soap_runtime_state.transport_uri[0] != '\0';
}

static unsigned int avtransport_current_actions(const PlayerSnapshot *snapshot)
{
    unsigned int actions = 0;

    if (!snapshot || !snapshot->has_media)
        return 0;

    switch (snapshot->state)
    {
    case PLAYER_STATE_STOPPED:
        actions |= AVTRANSPORT_ACTION_PLAY | AVTRANSPORT_ACTION_STOP;
        break;
    case PLAYER_STATE_LOADING:
        // Our SetAVTransportURI path preloads via mpv with pause=yes, so a
        // control point's immediate Play should still be accepted.
        actions |= AVTRANSPORT_ACTION_PLAY | AVTRANSPORT_ACTION_STOP;
        break;
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        actions |= AVTRANSPORT_ACTION_STOP;
        break;
    case PLAYER_STATE_PLAYING:
        actions |= AVTRANSPORT_ACTION_PAUSE | AVTRANSPORT_ACTION_STOP;
        if (snapshot->seekable)
            actions |= AVTRANSPORT_ACTION_SEEK;
        break;
    case PLAYER_STATE_PAUSED:
        actions |= AVTRANSPORT_ACTION_PLAY | AVTRANSPORT_ACTION_STOP;
        if (snapshot->seekable)
            actions |= AVTRANSPORT_ACTION_SEEK;
        break;
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_ERROR:
    default:
        break;
    }

    return actions;
}

static bool avtransport_action_available(unsigned int actions, AvTransportActionMask action)
{
    return (actions & (unsigned int)action) != 0;
}

static void avtransport_format_actions(unsigned int actions, char *out, size_t out_size)
{
    bool first = true;
    size_t used = 0;

    if (!out || out_size == 0)
        return;

    out[0] = '\0';

    if (avtransport_action_available(actions, AVTRANSPORT_ACTION_PLAY))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sPlay", first ? "" : ",");
        first = false;
    }
    if (used < out_size && avtransport_action_available(actions, AVTRANSPORT_ACTION_STOP))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sStop", first ? "" : ",");
        first = false;
    }
    if (used < out_size && avtransport_action_available(actions, AVTRANSPORT_ACTION_PAUSE))
    {
        used += (size_t)snprintf(out + used, out_size - used, "%sPause", first ? "" : ",");
        first = false;
    }
    if (used < out_size && avtransport_action_available(actions, AVTRANSPORT_ACTION_SEEK))
        (void)snprintf(out + used, out_size - used, "%sSeek", first ? "" : ",");
}

static bool avtransport_try_instance_id(const SoapActionContext *ctx,
                                        SoapActionOutput *out,
                                        const char *action_name,
                                        char *instance_id,
                                        size_t instance_id_size)
{
    if (!ctx || !out || !action_name || !instance_id || instance_id_size == 0)
        return false;

    if (soap_handler_try_arg(ctx, "InstanceID", instance_id, instance_id_size))
        return true;

    snprintf(instance_id, instance_id_size, "0");
    log_debug("[avtransport] default InstanceID=0 for %s\n", action_name);
    return true;
}

static char *avtransport_escape_alloc(const char *value, SoapActionOutput *out)
{
    const char *input = value ? value : "";
    size_t input_len = strlen(input);
    size_t out_size = input_len * 6 + 1;
    char *escaped = malloc(out_size);
    if (!escaped)
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return NULL;
    }

    if (!soap_handler_xml_escape(input, escaped, out_size))
    {
        free(escaped);
        soap_handler_set_fault(out, 501, "Action Failed");
        return NULL;
    }

    return escaped;
}

static bool avtransport_write_text_element(SoapActionOutput *out, const char *tag, const char *value)
{
    if (soap_writer_element_text(out, tag, value))
        return true;
    soap_handler_set_fault(out, 501, "Action Failed");
    return false;
}

static bool avtransport_write_raw_element(SoapActionOutput *out, const char *tag, const char *value)
{
    if (soap_writer_element_raw(out, tag, value))
        return true;
    soap_handler_set_fault(out, 501, "Action Failed");
    return false;
}

static bool avtransport_write_int_element(SoapActionOutput *out, const char *tag, long value)
{
    if (soap_writer_element_int(out, tag, value))
        return true;
    soap_handler_set_fault(out, 501, "Action Failed");
    return false;
}

static void avtransport_append_named_header(char *out, size_t out_size, const char *name, const char *value)
{
    size_t used;

    if (!out || out_size == 0 || !name || !value || value[0] == '\0')
        return;

    used = strlen(out);
    if (used >= out_size - 1)
        return;

    snprintf(out + used,
             out_size - used,
             "%s%s: %s",
             used > 0 ? "," : "",
             name,
             value);
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

static bool parse_hhmmss_to_ms(const char *value, int *out_ms)
{
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millis = 0;
    int parsed_chars = 0;
    const char *cursor = value;

    if (!value || !out_ms)
        return false;

    while (*cursor && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0')
        return false;

    if (sscanf(cursor, "%d:%d:%d%n", &hour, &minute, &second, &parsed_chars) != 3)
        return false;
    if (hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    if (cursor[parsed_chars] == '.')
    {
        const char *fraction = cursor + parsed_chars + 1;
        size_t frac_len = strlen(fraction);
        if (frac_len == 0 || frac_len > 3)
            return false;

        for (size_t i = 0; i < frac_len; ++i)
        {
            if (!isdigit((unsigned char)fraction[i]))
                return false;
            millis = millis * 10 + (fraction[i] - '0');
        }

        size_t pad_len = frac_len;
        while (pad_len < 3)
        {
            millis *= 10;
            ++pad_len;
        }
    }
    else if (cursor[parsed_chars] != '\0')
    {
        while (cursor[parsed_chars] && isspace((unsigned char)cursor[parsed_chars]))
            ++parsed_chars;
        if (cursor[parsed_chars] == '\0')
        {
            *out_ms = (hour * 3600 + minute * 60 + second) * 1000;
            return true;
        }
        return false;
    }

    *out_ms = (hour * 3600 + minute * 60 + second) * 1000 + millis;
    return true;
}

bool avtransport_set_uri(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char *uri = NULL;
    char *metadata = NULL;
    PlayerOpenContext open_ctx;
    char sender_ua_short[96];

    if (!ctx || !out)
        return false;

    memset(&open_ctx, 0, sizeof(open_ctx));

    uri = malloc(sizeof(g_soap_runtime_state.transport_uri));
    metadata = malloc(sizeof(g_soap_runtime_state.transport_uri_metadata));
    if (!uri || !metadata)
    {
        free(uri);
        free(metadata);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
    {
        free(uri);
        free(metadata);
        return false;
    }

    if (!soap_handler_require_arg(ctx, out, "CurrentURI", uri, sizeof(g_soap_runtime_state.transport_uri)))
    {
        free(uri);
        free(metadata);
        return false;
    }

    metadata[0] = '\0';
    soap_handler_extract_xml_value(ctx->body, "CurrentURIMetaData", metadata, sizeof(g_soap_runtime_state.transport_uri_metadata));

    (void)soap_handler_try_http_header(ctx, "User-Agent", open_ctx.sender_user_agent, sizeof(open_ctx.sender_user_agent));
    (void)soap_handler_try_http_header(ctx, "Cookie", open_ctx.cookie, sizeof(open_ctx.cookie));
    (void)soap_handler_try_http_header(ctx, "Referer", open_ctx.referrer, sizeof(open_ctx.referrer));
    (void)soap_handler_try_http_header(ctx, "Origin", open_ctx.origin, sizeof(open_ctx.origin));

    {
        char header_value[128];

        if (soap_handler_try_http_header(ctx, "X-Requested-With", header_value, sizeof(header_value)))
            avtransport_append_named_header(open_ctx.extra_headers, sizeof(open_ctx.extra_headers), "X-Requested-With", header_value);
        if (soap_handler_try_http_header(ctx, "X-Playback-Session-Id", header_value, sizeof(header_value)))
            avtransport_append_named_header(open_ctx.extra_headers, sizeof(open_ctx.extra_headers), "X-Playback-Session-Id", header_value);
    }

    sender_ua_short[0] = '\0';
    if (open_ctx.sender_user_agent[0] != '\0')
    {
        size_t sender_len = strlen(open_ctx.sender_user_agent);
        size_t copy_len = sender_len < sizeof(sender_ua_short) - 1 ? sender_len : sizeof(sender_ua_short) - 1;
        memcpy(sender_ua_short, open_ctx.sender_user_agent, copy_len);
        sender_ua_short[copy_len] = '\0';
        if (copy_len < sender_len && copy_len >= 3)
        {
            sender_ua_short[copy_len - 3] = '.';
            sender_ua_short[copy_len - 2] = '.';
            sender_ua_short[copy_len - 1] = '.';
        }
    }

    log_info("[avtransport] source_context sender_ua=%s cookie=%d extra_headers=%d referrer=%s origin=%s\n",
             sender_ua_short[0] != '\0' ? sender_ua_short : "<none>",
             open_ctx.cookie[0] != '\0' ? 1 : 0,
             open_ctx.extra_headers[0] != '\0' ? 1 : 0,
             open_ctx.referrer[0] != '\0' ? open_ctx.referrer : "<none>",
             open_ctx.origin[0] != '\0' ? open_ctx.origin : "<none>");

    if (!player_set_uri_with_context(uri, metadata, &open_ctx))
    {
        free(uri);
        free(metadata);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_uri, sizeof(g_soap_runtime_state.transport_uri), "%s", uri);
    snprintf(g_soap_runtime_state.transport_uri_metadata, sizeof(g_soap_runtime_state.transport_uri_metadata), "%s", metadata);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             "OK");

    free(uri);
    free(metadata);
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_play(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char speed[16];
    PlayerSnapshot snapshot;
    unsigned int actions;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Speed", speed, sizeof(speed)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    avtransport_get_snapshot(&snapshot);
    actions = avtransport_current_actions(&snapshot);
    if (snapshot.state == PLAYER_STATE_PLAYING)
    {
        soap_handler_set_success(out, "");
        return true;
    }
    if (!avtransport_action_available(actions, AVTRANSPORT_ACTION_PLAY))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_play())
    {
        soap_handler_set_fault(out, 704, "Playing failed");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_speed, sizeof(g_soap_runtime_state.transport_speed), "%s", speed);
    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             "OK");
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_pause(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    PlayerSnapshot snapshot;
    unsigned int actions;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    avtransport_get_snapshot(&snapshot);
    sync_transport_state_from_player();
    actions = avtransport_current_actions(&snapshot);

    if (snapshot.state == PLAYER_STATE_PAUSED)
    {
        soap_handler_set_success(out, "");
        return true;
    }
    if (!avtransport_action_available(actions, AVTRANSPORT_ACTION_PAUSE))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_pause())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             "OK");
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_stop(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    PlayerSnapshot snapshot;
    unsigned int actions;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    avtransport_get_snapshot(&snapshot);
    sync_transport_state_from_player();
    actions = avtransport_current_actions(&snapshot);

    if (snapshot.state == PLAYER_STATE_STOPPED)
    {
        soap_handler_set_success(out, "");
        return true;
    }
    if (!avtransport_action_available(actions, AVTRANSPORT_ACTION_STOP))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!player_stop())
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             "OK");
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_get_transport_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    // Some mobile control points omit InstanceID for read-only queries.
    // Treat missing value as "0" for compatibility.
    if (!avtransport_try_instance_id(ctx, out, "GetTransportInfo", instance_id, sizeof(instance_id)))
        return false;

    PlayerState player_state = sync_transport_state_from_player();
    const char *transport_state = transport_state_from_player_state(player_state);

    soap_writer_clear(out);
    if (!avtransport_write_text_element(out, "CurrentTransportState", transport_state) ||
        !avtransport_write_text_element(out, "CurrentTransportStatus", g_soap_runtime_state.transport_status) ||
        !avtransport_write_text_element(out, "CurrentSpeed", g_soap_runtime_state.transport_speed))
    {
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_current_transport_actions(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    PlayerSnapshot snapshot;
    unsigned int actions;
    char action_list[64];
    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetCurrentTransportActions", instance_id, sizeof(instance_id)))
        return false;

    avtransport_get_snapshot(&snapshot);
    sync_transport_state_from_player();
    actions = avtransport_current_actions(&snapshot);
    avtransport_format_actions(actions, action_list, sizeof(action_list));

    soap_writer_clear(out);
    if (!avtransport_write_text_element(out, "Actions", action_list))
    {
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_media_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetMediaInfo", instance_id, sizeof(instance_id)))
        return false;

    PlayerSnapshot snapshot;
    int duration_ms = 0;

    if (player_get_snapshot(&snapshot))
        duration_ms = snapshot.duration_ms;
    else
        duration_ms = player_get_duration_ms();

    char duration_str[16];
    format_hhmmss_from_ms(duration_ms, duration_str, sizeof(duration_str));
    snprintf(g_soap_runtime_state.transport_duration, sizeof(g_soap_runtime_state.transport_duration), "%s", duration_str);

    char *escaped_uri = avtransport_escape_alloc(g_soap_runtime_state.transport_uri, out);
    char *escaped_metadata = avtransport_escape_alloc(g_soap_runtime_state.transport_uri_metadata, out);
    if (!escaped_uri || !escaped_metadata)
    {
        free(escaped_uri);
        free(escaped_metadata);
        return false;
    }

    soap_writer_clear(out);
    bool ok = avtransport_write_int_element(out, "NrTracks", 1) &&
              avtransport_write_text_element(out, "MediaDuration", duration_str) &&
              avtransport_write_raw_element(out, "CurrentURI", escaped_uri) &&
              avtransport_write_raw_element(out, "CurrentURIMetaData", escaped_metadata) &&
              avtransport_write_text_element(out, "NextURI", "") &&
              avtransport_write_text_element(out, "NextURIMetaData", "") &&
              avtransport_write_text_element(out, "PlayMedium", "NETWORK") &&
              avtransport_write_text_element(out, "RecordMedium", "NOT_IMPLEMENTED") &&
              avtransport_write_text_element(out, "WriteStatus", "NOT_IMPLEMENTED");
    free(escaped_uri);
    free(escaped_metadata);
    if (!ok)
        return false;

    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_position_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];

    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetPositionInfo", instance_id, sizeof(instance_id)))
        return false;

    PlayerSnapshot snapshot;
    int position_ms = 0;
    int duration_ms = 0;

    if (player_get_snapshot(&snapshot))
    {
        position_ms = snapshot.position_ms;
        duration_ms = snapshot.duration_ms;
    }
    else
    {
        position_ms = player_get_position_ms();
        duration_ms = player_get_duration_ms();
    }

    char duration_str[16];
    char rel_time_str[16];
    char abs_time_str[16];

    format_hhmmss_from_ms(duration_ms, duration_str, sizeof(duration_str));
    format_hhmmss_from_ms(position_ms, rel_time_str, sizeof(rel_time_str));
    format_hhmmss_from_ms(position_ms, abs_time_str, sizeof(abs_time_str));

    snprintf(g_soap_runtime_state.transport_duration, sizeof(g_soap_runtime_state.transport_duration), "%s", duration_str);
    snprintf(g_soap_runtime_state.transport_rel_time, sizeof(g_soap_runtime_state.transport_rel_time), "%s", rel_time_str);
    snprintf(g_soap_runtime_state.transport_abs_time, sizeof(g_soap_runtime_state.transport_abs_time), "%s", abs_time_str);

    char *escaped_uri = avtransport_escape_alloc(g_soap_runtime_state.transport_uri, out);
    char *escaped_metadata = avtransport_escape_alloc(g_soap_runtime_state.transport_uri_metadata, out);
    if (!escaped_uri || !escaped_metadata)
    {
        free(escaped_uri);
        free(escaped_metadata);
        return false;
    }

    soap_writer_clear(out);
    bool ok = avtransport_write_int_element(out, "Track", 1) &&
              avtransport_write_text_element(out, "TrackDuration", duration_str) &&
              avtransport_write_raw_element(out, "TrackMetaData", escaped_metadata) &&
              avtransport_write_raw_element(out, "TrackURI", escaped_uri) &&
              avtransport_write_text_element(out, "RelTime", rel_time_str) &&
              avtransport_write_text_element(out, "AbsTime", abs_time_str) &&
              avtransport_write_int_element(out, "RelCount", 0) &&
              avtransport_write_int_element(out, "AbsCount", 0);
    free(escaped_uri);
    free(escaped_metadata);
    if (!ok)
        return false;

    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_seek(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char unit[32];
    char target[32];
    PlayerSnapshot snapshot;
    unsigned int actions;

    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "Seek", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_try_arg(ctx, "Unit", unit, sizeof(unit)))
    {
        snprintf(unit, sizeof(unit), "REL_TIME");
        log_debug("[avtransport] default Unit=REL_TIME for Seek\n");
    }

    if (!soap_handler_try_arg(ctx, "Target", target, sizeof(target)))
    {
        snprintf(target, sizeof(target), "00:00:00");
        log_debug("[avtransport] default Target=00:00:00 for Seek\n");
    }

    if (strcasecmp(unit, "REL_TIME") != 0 && strcasecmp(unit, "ABS_TIME") != 0)
    {
        soap_handler_set_fault(out, 710, "Seek mode not supported");
        return false;
    }

    if (g_soap_runtime_state.transport_uri[0] == '\0')
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    avtransport_get_snapshot(&snapshot);
    sync_transport_state_from_player();
    actions = avtransport_current_actions(&snapshot);

    if (!avtransport_action_available(actions, AVTRANSPORT_ACTION_SEEK))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    int target_ms = 0;
    if (!parse_hhmmss_to_ms(target, &target_ms))
    {
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    if (!player_seek_ms(target_ms))
    {
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    snprintf(g_soap_runtime_state.transport_state,
             sizeof(g_soap_runtime_state.transport_state),
             "%s",
             transport_state_from_player_state(player_get_state()));
    snprintf(g_soap_runtime_state.transport_status,
             sizeof(g_soap_runtime_state.transport_status),
             "%s",
             "OK");
    soap_handler_set_success(out, "");
    return true;
}
