#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"
#include "player/renderer.h"

typedef enum
{
    DLNA_PROTOCOL_SERVICE_INVALID = -1,
    DLNA_PROTOCOL_SERVICE_AVTRANSPORT = 0,
    DLNA_PROTOCOL_SERVICE_RENDERINGCONTROL,
    DLNA_PROTOCOL_SERVICE_CONNECTIONMANAGER
} DlnaProtocolService;

typedef enum
{
    DLNA_STATE_TYPE_BOOLEAN = 0,
    DLNA_STATE_TYPE_I2,
    DLNA_STATE_TYPE_UI2,
    DLNA_STATE_TYPE_I4,
    DLNA_STATE_TYPE_UI4,
    DLNA_STATE_TYPE_STRING
} DlnaStateDataType;

typedef struct
{
    const char *name;
    DlnaProtocolService service;
    bool send_events;
    DlnaStateDataType datatype;
    bool has_range;
    int minimum;
    int maximum;
    int step;
    const char *const *allowed_values;
    size_t allowed_value_count;
    const char *string_value;
    int int_value;
    bool bool_value;
} DlnaStateVariableView;

typedef struct
{
    const char *transport_state;
    const char *transport_status;
    const char *transport_play_speed;
    const char *current_play_mode;
    const char *av_transport_uri;
    const char *av_transport_uri_metadata;
    const char *next_av_transport_uri;
    const char *next_av_transport_uri_metadata;
    const char *current_track_uri;
    const char *current_track_metadata;
    const char *current_track_title;
    const char *current_media_duration;
    const char *current_track_duration;
    const char *relative_time_position;
    const char *absolute_time_position;
    int current_track;
    int number_of_tracks;
    int relative_counter_position;
    int absolute_counter_position;
    int volume;
    bool mute;
    const char *a_arg_type_direction;
    const char *playback_storage_medium;
    const char *source_protocol_info;
    const char *sink_protocol_info;
    const char *current_connection_ids;
} DlnaProtocolStateView;

// Macast-style protocol state storage backed by per-variable metadata and
// runtime values loaded from service XML templates.
void dlna_protocol_state_init(void);
void dlna_protocol_state_reset(void);
void dlna_protocol_state_sync_from_renderer(void);
void dlna_protocol_state_on_renderer_event(const RendererEvent *event);
void dlna_protocol_state_apply_set_uri(const char *uri, const char *metadata);
void dlna_protocol_state_apply_play(void);
void dlna_protocol_state_apply_pause(void);
void dlna_protocol_state_apply_stop(void);
void dlna_protocol_state_apply_seek_target(const char *target);
void dlna_protocol_state_set_transport_status(const char *status);
void dlna_protocol_state_set_transport_speed(const char *speed);
void dlna_protocol_state_set_transport_timing(int duration_ms, int position_ms);
void dlna_protocol_state_set_source_protocol_info(const char *value);
void dlna_protocol_state_set_sink_protocol_info(const char *value);
void dlna_protocol_state_set_connection_ids(const char *value);
const DlnaProtocolStateView *dlna_protocol_state_view(void);

size_t dlna_protocol_state_variable_count(void);
const DlnaStateVariableView *dlna_protocol_state_variable_at(size_t index);
const DlnaStateVariableView *dlna_protocol_state_find_variable(const char *name);

const char *dlna_protocol_transport_state_from_renderer_state(RendererState state);
const char *dlna_protocol_transport_status_from_renderer_state(RendererState state);
char *dlna_protocol_format_hhmmss_alloc(int value_ms);
