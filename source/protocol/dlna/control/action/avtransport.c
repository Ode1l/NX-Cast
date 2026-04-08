#include "../handler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../handler_internal.h"
#include "log/log.h"
#include "player/renderer.h"

#define AVTRANSPORT_COUNTER_UNKNOWN 2147483647

static RendererState sync_transport_state_from_renderer(void)
{
    RendererSnapshot snapshot;
    RendererState renderer_state = PLAYER_STATE_IDLE;

    if (renderer_get_snapshot(&snapshot))
    {
        renderer_state = snapshot.state;
        renderer_snapshot_clear(&snapshot);
    }
    else
        renderer_state = renderer_get_state();

    dlna_protocol_state_sync_from_renderer();
    return renderer_state;
}

static void avtransport_get_snapshot(RendererSnapshot *snapshot)
{
    if (!snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    if (renderer_get_snapshot(snapshot))
        return;

    snapshot->state = renderer_get_state();
    snapshot->position_ms = renderer_get_position_ms();
    snapshot->duration_ms = renderer_get_duration_ms();
    snapshot->volume = renderer_get_volume();
    snapshot->mute = renderer_get_mute();
    snapshot->seekable = renderer_is_seekable();
    snapshot->has_media = dlna_protocol_state_view()->av_transport_uri[0] != '\0';
}

static unsigned int avtransport_current_actions(const RendererSnapshot *snapshot)
{
    unsigned int actions = 0;

    if (!snapshot || !snapshot->has_media)
        return 0;

    switch (snapshot->state)
    {
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_PAUSED:
        actions |= 1u << 0; /* Play */
        actions |= 1u << 1; /* Stop */
        if (snapshot->seekable)
            actions |= 1u << 3; /* Seek */
        break;
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_SEEKING:
        actions |= 1u << 1; /* Stop */
        break;
    case PLAYER_STATE_PLAYING:
        actions |= 1u << 1; /* Stop */
        actions |= 1u << 2; /* Pause */
        if (snapshot->seekable)
            actions |= 1u << 3; /* Seek */
        break;
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_ERROR:
    default:
        break;
    }

    return actions;
}

static char *avtransport_format_actions_alloc(unsigned int actions)
{
    bool first = true;
    char *out = strdup("");
    char *next = NULL;

    if (!out)
        return NULL;

    if (actions & (1u << 0))
    {
        next = soap_handler_strdup_printf("%s%sPlay", out, first ? "" : ",");
        free(out);
        out = next;
        if (!out)
            return NULL;
        first = false;
    }
    if (actions & (1u << 1))
    {
        next = soap_handler_strdup_printf("%s%sStop", out, first ? "" : ",");
        free(out);
        out = next;
        if (!out)
            return NULL;
        first = false;
    }
    if (actions & (1u << 2))
    {
        next = soap_handler_strdup_printf("%s%sPause", out, first ? "" : ",");
        free(out);
        out = next;
        if (!out)
            return NULL;
        first = false;
    }
    if (actions & (1u << 3))
    {
        next = soap_handler_strdup_printf("%s%sSeek", out, first ? "" : ",");
        free(out);
        out = next;
    }

    return out;
}

static bool avtransport_try_instance_id(const SoapActionContext *ctx,
                                        SoapActionOutput *out,
                                        const char *action_name,
                                        char **instance_id_out)
{
    if (!ctx || !out || !action_name || !instance_id_out)
        return false;

    *instance_id_out = NULL;
    if (soap_handler_try_arg_alloc(ctx, "InstanceID", instance_id_out))
        return true;

    *instance_id_out = strdup("0");
    if (!*instance_id_out)
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }
    log_debug("[avtransport] default InstanceID=0 for %s\n", action_name);
    return true;
}

static char *avtransport_escape_alloc(const char *value, SoapActionOutput *out)
{
    char *escaped = soap_handler_xml_escape_alloc(value);
    if (!escaped)
    {
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
    char *instance_id = NULL;
    char *uri = NULL;
    char *metadata = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "CurrentURI", &uri))
    {
        free(instance_id);
        return false;
    }

    if (!soap_handler_try_arg_alloc(ctx, "CurrentURIMetaData", &metadata))
    {
        metadata = strdup("");
        if (!metadata)
        {
            free(instance_id);
            free(uri);
            soap_handler_set_fault(out, 501, "Action Failed");
            return false;
        }
    }

    if (!renderer_set_uri(uri, metadata))
    {
        free(instance_id);
        free(uri);
        free(metadata);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    dlna_protocol_state_apply_set_uri(uri, metadata);

    free(instance_id);
    free(uri);
    free(metadata);
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_play(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *speed = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "Speed", &speed))
    {
        free(instance_id);
        return false;
    }

    if (state->av_transport_uri[0] == '\0')
    {
        free(instance_id);
        free(speed);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!renderer_play())
    {
        free(instance_id);
        free(speed);
        soap_handler_set_fault(out, 704, "Playing failed");
        return false;
    }

    dlna_protocol_state_set_transport_speed(speed);
    dlna_protocol_state_apply_play();
    free(instance_id);
    free(speed);
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_pause(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (state->av_transport_uri[0] == '\0')
    {
        free(instance_id);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    if (!renderer_pause())
    {
        free(instance_id);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    dlna_protocol_state_apply_pause();
    free(instance_id);
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_stop(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!renderer_stop())
    {
        free(instance_id);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    dlna_protocol_state_apply_stop();
    free(instance_id);
    soap_handler_set_success(out, "");
    return true;
}

bool avtransport_get_transport_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;

    if (!ctx || !out)
        return false;

    // Some mobile control points omit InstanceID for read-only queries.
    // Treat missing value as "0" for compatibility.
    if (!avtransport_try_instance_id(ctx, out, "GetTransportInfo", &instance_id))
        return false;

    RendererState renderer_state = sync_transport_state_from_renderer();
    const DlnaProtocolStateView *state = dlna_protocol_state_view();
    (void)renderer_state;

    soap_writer_clear(out);
    if (!avtransport_write_text_element(out, "CurrentTransportState", state->transport_state) ||
        !avtransport_write_text_element(out, "CurrentTransportStatus", state->transport_status) ||
        !avtransport_write_text_element(out, "CurrentSpeed", state->transport_play_speed))
    {
        free(instance_id);
        return false;
    }

    free(instance_id);
    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_current_transport_actions(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    RendererSnapshot snapshot;
    unsigned int actions;
    char *action_list = NULL;
    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetCurrentTransportActions", &instance_id))
        return false;

    avtransport_get_snapshot(&snapshot);
    sync_transport_state_from_renderer();
    actions = avtransport_current_actions(&snapshot);
    action_list = avtransport_format_actions_alloc(actions);
    renderer_snapshot_clear(&snapshot);
    if (!action_list)
    {
        free(instance_id);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_writer_clear(out);
    if (!avtransport_write_text_element(out, "Actions", action_list))
    {
        free(instance_id);
        free(action_list);
        return false;
    }

    free(instance_id);
    free(action_list);
    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_media_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetMediaInfo", &instance_id))
        return false;

    RendererSnapshot snapshot;
    int duration_ms = 0;
    int position_ms = 0;

    memset(&snapshot, 0, sizeof(snapshot));
    if (renderer_get_snapshot(&snapshot))
    {
        duration_ms = snapshot.duration_ms;
        position_ms = snapshot.position_ms;
        renderer_snapshot_clear(&snapshot);
    }
    else
    {
        duration_ms = renderer_get_duration_ms();
        position_ms = renderer_get_position_ms();
    }

    dlna_protocol_state_set_transport_timing(duration_ms, position_ms);

    char *escaped_uri = avtransport_escape_alloc(state->av_transport_uri, out);
    char *escaped_metadata = avtransport_escape_alloc(state->av_transport_uri_metadata, out);
    if (!escaped_uri || !escaped_metadata)
    {
        free(instance_id);
        free(escaped_uri);
        free(escaped_metadata);
        return false;
    }

    soap_writer_clear(out);
    bool ok = avtransport_write_int_element(out, "NrTracks", state->number_of_tracks) &&
              avtransport_write_text_element(out, "MediaDuration", state->current_media_duration) &&
              avtransport_write_raw_element(out, "CurrentURI", escaped_uri) &&
              avtransport_write_raw_element(out, "CurrentURIMetaData", escaped_metadata) &&
              avtransport_write_text_element(out, "NextURI", state->next_av_transport_uri) &&
              avtransport_write_text_element(out, "NextURIMetaData", state->next_av_transport_uri_metadata) &&
              avtransport_write_text_element(out, "PlayMedium", state->playback_storage_medium) &&
              avtransport_write_text_element(out, "RecordMedium", "NOT_IMPLEMENTED") &&
              avtransport_write_text_element(out, "WriteStatus", "NOT_IMPLEMENTED");
    free(escaped_uri);
    free(escaped_metadata);
    if (!ok)
    {
        free(instance_id);
        return false;
    }

    free(instance_id);
    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_get_position_info(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();
    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "GetPositionInfo", &instance_id))
        return false;

    RendererSnapshot snapshot;
    bool has_snapshot = false;
    int position_ms = 0;
    int duration_ms = 0;

    memset(&snapshot, 0, sizeof(snapshot));
    has_snapshot = renderer_get_snapshot(&snapshot);
    if (has_snapshot)
    {
        position_ms = snapshot.position_ms;
        duration_ms = snapshot.duration_ms;
        renderer_snapshot_clear(&snapshot);
    }
    else
    {
        position_ms = renderer_get_position_ms();
        duration_ms = renderer_get_duration_ms();
    }

    dlna_protocol_state_set_transport_timing(duration_ms, position_ms);
    (void)has_snapshot;

    char *escaped_uri = avtransport_escape_alloc(state->current_track_uri, out);
    char *escaped_metadata = avtransport_escape_alloc(state->current_track_metadata, out);
    if (!escaped_uri || !escaped_metadata)
    {
        free(instance_id);
        free(escaped_uri);
        free(escaped_metadata);
        return false;
    }

    soap_writer_clear(out);
    bool ok = avtransport_write_int_element(out, "Track", state->current_track) &&
              avtransport_write_text_element(out, "TrackDuration", state->current_track_duration) &&
              avtransport_write_raw_element(out, "TrackMetaData", escaped_metadata) &&
              avtransport_write_raw_element(out, "TrackURI", escaped_uri) &&
              avtransport_write_text_element(out, "RelTime", state->relative_time_position) &&
              avtransport_write_text_element(out, "AbsTime", state->absolute_time_position) &&
              avtransport_write_int_element(out, "RelCount", state->relative_counter_position) &&
              avtransport_write_int_element(out, "AbsCount", state->absolute_counter_position);
    free(escaped_uri);
    free(escaped_metadata);
    if (!ok)
    {
        free(instance_id);
        return false;
    }

    free(instance_id);
    soap_handler_set_success(out, NULL);
    return true;
}

bool avtransport_seek(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *unit = NULL;
    char *target = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!avtransport_try_instance_id(ctx, out, "Seek", &instance_id))
        return false;

    if (!soap_handler_try_arg_alloc(ctx, "Unit", &unit))
    {
        unit = strdup("REL_TIME");
        if (!unit)
        {
            free(instance_id);
            soap_handler_set_fault(out, 501, "Action Failed");
            return false;
        }
        log_debug("[avtransport] default Unit=REL_TIME for Seek\n");
    }

    if (!soap_handler_try_arg_alloc(ctx, "Target", &target))
    {
        target = strdup("00:00:00");
        if (!target)
        {
            free(instance_id);
            free(unit);
            soap_handler_set_fault(out, 501, "Action Failed");
            return false;
        }
        log_debug("[avtransport] default Target=00:00:00 for Seek\n");
    }

    if (strcasecmp(unit, "REL_TIME") != 0 && strcasecmp(unit, "ABS_TIME") != 0)
    {
        free(instance_id);
        free(unit);
        free(target);
        soap_handler_set_fault(out, 710, "Seek mode not supported");
        return false;
    }

    if (state->av_transport_uri[0] == '\0')
    {
        free(instance_id);
        free(unit);
        free(target);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    int target_ms = 0;
    if (!parse_hhmmss_to_ms(target, &target_ms))
    {
        free(instance_id);
        free(unit);
        free(target);
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    if (!renderer_seek_ms(target_ms))
    {
        free(instance_id);
        free(unit);
        free(target);
        soap_handler_set_fault(out, 701, "Transition not available");
        return false;
    }

    dlna_protocol_state_apply_seek_target(target);
    dlna_protocol_state_set_transport_status("OK");
    free(instance_id);
    free(unit);
    free(target);
    soap_handler_set_success(out, "");
    return true;
}
