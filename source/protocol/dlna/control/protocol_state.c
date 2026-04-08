#include "protocol_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "log/log.h"
#include "protocol/dlna/description/template_resource.h"

#define DLNA_COUNTER_UNKNOWN 2147483647
#define DLNA_SERVICE_XML_COUNT 3
#define DLNA_PROTOCOL_STATE_INITIAL_CAPACITY 48
#define DLNA_FALLBACK_SINK_PROTOCOL_INFO \
    "http-get:*:audio/mpeg:*," \
    "http-get:*:audio/mp4:*," \
    "http-get:*:audio/x-m4a:*," \
    "http-get:*:audio/aac:*," \
    "http-get:*:audio/flac:*," \
    "http-get:*:audio/x-flac:*," \
    "http-get:*:audio/wav:*," \
    "http-get:*:audio/x-wav:*," \
    "http-get:*:audio/ogg:*," \
    "http-get:*:audio/vnd.dlna.adts:*," \
    "http-get:*:audio/x-mpegurl:*," \
    "http-get:*:audio/mpegurl:*," \
    "http-get:*:video/mp4:*," \
    "http-get:*:video/x-m4v:*," \
    "http-get:*:video/mpeg:*," \
    "http-get:*:video/mp2t:*," \
    "http-get:*:video/quicktime:*," \
    "http-get:*:video/webm:*," \
    "http-get:*:video/x-matroska:*," \
    "http-get:*:video/x-msvideo:*," \
    "http-get:*:video/vnd.dlna.mpeg-tts:*," \
    "http-get:*:video/m3u8:*," \
    "http-get:*:video/flv:*," \
    "http-get:*:video/x-flv:*," \
    "http-get:*:application/vnd.apple.mpegurl:*," \
    "http-get:*:application/x-mpegURL:*"

typedef struct
{
    DlnaStateVariableView view;
    char **owned_allowed_values;
} DlnaStateVariable;

typedef struct
{
    DlnaStateVariable *items;
    size_t count;
    size_t capacity;
} DlnaProtocolStateStore;

typedef struct
{
    DlnaStateVariable *transport_state;
    DlnaStateVariable *transport_status;
    DlnaStateVariable *transport_play_speed;
    DlnaStateVariable *current_play_mode;
    DlnaStateVariable *av_transport_uri;
    DlnaStateVariable *av_transport_uri_metadata;
    DlnaStateVariable *next_av_transport_uri;
    DlnaStateVariable *next_av_transport_uri_metadata;
    DlnaStateVariable *current_track_uri;
    DlnaStateVariable *current_track_metadata;
    DlnaStateVariable *current_track_title;
    DlnaStateVariable *current_media_duration;
    DlnaStateVariable *current_track_duration;
    DlnaStateVariable *relative_time_position;
    DlnaStateVariable *absolute_time_position;
    DlnaStateVariable *current_track;
    DlnaStateVariable *number_of_tracks;
    DlnaStateVariable *relative_counter_position;
    DlnaStateVariable *absolute_counter_position;
    DlnaStateVariable *volume;
    DlnaStateVariable *mute;
    DlnaStateVariable *a_arg_type_direction;
    DlnaStateVariable *playback_storage_medium;
    DlnaStateVariable *source_protocol_info;
    DlnaStateVariable *sink_protocol_info;
    DlnaStateVariable *current_connection_ids;
} DlnaProtocolStateRefs;

typedef struct
{
    const char *template_path;
    DlnaProtocolService service;
} DlnaServiceTemplate;

static const DlnaServiceTemplate g_service_templates[DLNA_SERVICE_XML_COUNT] = {
    {"AVTransport.xml", DLNA_PROTOCOL_SERVICE_AVTRANSPORT},
    {"RenderingControl.xml", DLNA_PROTOCOL_SERVICE_RENDERINGCONTROL},
    {"ConnectionManager.xml", DLNA_PROTOCOL_SERVICE_CONNECTIONMANAGER},
};

static DlnaProtocolStateStore g_state_store = {0};
static DlnaProtocolStateRefs g_refs = {0};
static DlnaProtocolStateView g_state_view = {0};
static bool g_state_initialized = false;
static const char g_empty_string[] = "";

static bool should_apply_transport_state_from_renderer(RendererState state)
{
    switch (state)
    {
    case PLAYER_STATE_PLAYING:
    case PLAYER_STATE_PAUSED:
    case PLAYER_STATE_STOPPED:
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_ERROR:
        return true;
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
    default:
        return false;
    }
}

static char *protocol_strdup_printf(const char *fmt, ...)
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

static void reset_state_view_defaults(void)
{
    memset(&g_state_view, 0, sizeof(g_state_view));
    g_state_view.transport_state = g_empty_string;
    g_state_view.transport_status = g_empty_string;
    g_state_view.transport_play_speed = g_empty_string;
    g_state_view.current_play_mode = g_empty_string;
    g_state_view.av_transport_uri = g_empty_string;
    g_state_view.av_transport_uri_metadata = g_empty_string;
    g_state_view.next_av_transport_uri = g_empty_string;
    g_state_view.next_av_transport_uri_metadata = g_empty_string;
    g_state_view.current_track_uri = g_empty_string;
    g_state_view.current_track_metadata = g_empty_string;
    g_state_view.current_track_title = g_empty_string;
    g_state_view.current_media_duration = g_empty_string;
    g_state_view.current_track_duration = g_empty_string;
    g_state_view.relative_time_position = g_empty_string;
    g_state_view.absolute_time_position = g_empty_string;
    g_state_view.a_arg_type_direction = g_empty_string;
    g_state_view.playback_storage_medium = g_empty_string;
    g_state_view.source_protocol_info = g_empty_string;
    g_state_view.sink_protocol_info = g_empty_string;
    g_state_view.current_connection_ids = g_empty_string;
}

static char *dup_range(const char *start, const char *end)
{
    size_t len;
    char *value;

    if (!start || !end || end < start)
        return NULL;

    len = (size_t)(end - start);
    value = malloc(len + 1);
    if (!value)
        return NULL;

    memcpy(value, start, len);
    value[len] = '\0';
    return value;
}

static void trim_in_place(char *value)
{
    size_t len;
    size_t start = 0;

    if (!value)
        return;

    len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r' || value[len - 1] == '\n'))
        value[--len] = '\0';

    while (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')
        ++start;

    if (start > 0)
        memmove(value, value + start, strlen(value + start) + 1);
}

static bool extract_tag_text_dup(const char *scope_start,
                                 const char *scope_end,
                                 const char *tag,
                                 char **out)
{
    char *open_tag;
    char *close_tag;
    const char *start;
    const char *end;

    if (!scope_start || !scope_end || !tag || !out)
        return false;

    *out = NULL;
    open_tag = protocol_strdup_printf("<%s>", tag);
    close_tag = protocol_strdup_printf("</%s>", tag);
    if (!open_tag || !close_tag)
    {
        free(open_tag);
        free(close_tag);
        return false;
    }

    start = strstr(scope_start, open_tag);
    if (!start || start >= scope_end)
    {
        free(open_tag);
        free(close_tag);
        return false;
    }
    start += strlen(open_tag);

    end = strstr(start, close_tag);
    if (!end || end > scope_end)
    {
        free(open_tag);
        free(close_tag);
        return false;
    }

    *out = dup_range(start, end);
    free(open_tag);
    free(close_tag);
    if (!*out)
        return false;
    trim_in_place(*out);
    return true;
}

static bool extract_send_events_value(const char *tag_start, const char *tag_end, bool *out_send_events)
{
    const char *attr;
    const char *value_start;
    const char *value_end;

    if (!tag_start || !tag_end || !out_send_events)
        return false;

    attr = strstr(tag_start, "sendEvents=\"");
    if (!attr || attr >= tag_end)
    {
        *out_send_events = false;
        return true;
    }

    value_start = attr + strlen("sendEvents=\"");
    value_end = strchr(value_start, '"');
    if (!value_end || value_end > tag_end)
        return false;

    *out_send_events = (size_t)(value_end - value_start) == 3 &&
                       strncasecmp(value_start, "yes", 3) == 0;
    return true;
}

static DlnaStateDataType parse_datatype(const char *value)
{
    if (!value)
        return DLNA_STATE_TYPE_STRING;
    if (strcmp(value, "boolean") == 0)
        return DLNA_STATE_TYPE_BOOLEAN;
    if (strcmp(value, "i2") == 0)
        return DLNA_STATE_TYPE_I2;
    if (strcmp(value, "ui2") == 0)
        return DLNA_STATE_TYPE_UI2;
    if (strcmp(value, "i4") == 0)
        return DLNA_STATE_TYPE_I4;
    if (strcmp(value, "ui4") == 0)
        return DLNA_STATE_TYPE_UI4;
    return DLNA_STATE_TYPE_STRING;
}

static bool parse_boolean_value(const char *value)
{
    if (!value)
        return false;
    return strcmp(value, "1") == 0 ||
           strcasecmp(value, "true") == 0 ||
           strcasecmp(value, "yes") == 0;
}

static int parse_int_value(const char *value)
{
    if (!value || value[0] == '\0')
        return 0;
    return (int)strtol(value, NULL, 10);
}

static void free_state_variable(DlnaStateVariable *variable)
{
    if (!variable)
        return;

    free((char *)variable->view.name);
    if (variable->view.datatype == DLNA_STATE_TYPE_STRING)
        free((char *)variable->view.string_value);

    if (variable->owned_allowed_values)
    {
        for (size_t i = 0; i < variable->view.allowed_value_count; ++i)
            free(variable->owned_allowed_values[i]);
        free(variable->owned_allowed_values);
    }

    memset(variable, 0, sizeof(*variable));
}

static void clear_state_store(void)
{
    for (size_t i = 0; i < g_state_store.count; ++i)
        free_state_variable(&g_state_store.items[i]);

    free(g_state_store.items);
    memset(&g_state_store, 0, sizeof(g_state_store));
    memset(&g_refs, 0, sizeof(g_refs));
    reset_state_view_defaults();
    g_state_initialized = false;
}

static bool ensure_state_capacity(size_t capacity)
{
    DlnaStateVariable *next_items;
    size_t next_capacity;

    if (capacity <= g_state_store.capacity)
        return true;

    next_capacity = g_state_store.capacity == 0 ? DLNA_PROTOCOL_STATE_INITIAL_CAPACITY : g_state_store.capacity * 2;
    while (next_capacity < capacity)
        next_capacity *= 2;

    next_items = realloc(g_state_store.items, next_capacity * sizeof(*next_items));
    if (!next_items)
        return false;

    g_state_store.items = next_items;
    g_state_store.capacity = next_capacity;
    return true;
}

static DlnaStateVariable *find_state_variable_mutable(const char *name)
{
    if (!name)
        return NULL;

    for (size_t i = 0; i < g_state_store.count; ++i)
    {
        if (strcmp(g_state_store.items[i].view.name, name) == 0)
            return &g_state_store.items[i];
    }

    return NULL;
}

static const DlnaStateVariable *find_state_variable_const_internal(const char *name)
{
    return find_state_variable_mutable(name);
}

static bool set_state_string_value(DlnaStateVariable *variable, const char *value)
{
    char *copy;

    if (!variable || variable->view.datatype != DLNA_STATE_TYPE_STRING)
        return false;

    copy = strdup(value ? value : "");
    if (!copy)
        return false;

    free((char *)variable->view.string_value);
    variable->view.string_value = copy;
    return true;
}

static void set_state_int_value(DlnaStateVariable *variable, int value)
{
    if (!variable)
        return;

    variable->view.int_value = value;
    variable->view.bool_value = value != 0;
}

static void set_state_bool_value(DlnaStateVariable *variable, bool value)
{
    if (!variable)
        return;

    variable->view.bool_value = value;
    variable->view.int_value = value ? 1 : 0;
}

static const char *string_or_empty(const DlnaStateVariable *variable)
{
    if (!variable || variable->view.datatype != DLNA_STATE_TYPE_STRING || !variable->view.string_value)
        return "";
    return variable->view.string_value;
}

static int int_or_zero(const DlnaStateVariable *variable)
{
    if (!variable)
        return 0;
    return variable->view.int_value;
}

static bool bool_or_false(const DlnaStateVariable *variable)
{
    if (!variable)
        return false;
    return variable->view.bool_value;
}

static void refresh_state_view(void)
{
    g_state_view.transport_state = string_or_empty(g_refs.transport_state);
    g_state_view.transport_status = string_or_empty(g_refs.transport_status);
    g_state_view.transport_play_speed = string_or_empty(g_refs.transport_play_speed);
    g_state_view.current_play_mode = string_or_empty(g_refs.current_play_mode);
    g_state_view.av_transport_uri = string_or_empty(g_refs.av_transport_uri);
    g_state_view.av_transport_uri_metadata = string_or_empty(g_refs.av_transport_uri_metadata);
    g_state_view.next_av_transport_uri = string_or_empty(g_refs.next_av_transport_uri);
    g_state_view.next_av_transport_uri_metadata = string_or_empty(g_refs.next_av_transport_uri_metadata);
    g_state_view.current_track_uri = string_or_empty(g_refs.current_track_uri);
    g_state_view.current_track_metadata = string_or_empty(g_refs.current_track_metadata);
    g_state_view.current_track_title = string_or_empty(g_refs.current_track_title);
    g_state_view.current_media_duration = string_or_empty(g_refs.current_media_duration);
    g_state_view.current_track_duration = string_or_empty(g_refs.current_track_duration);
    g_state_view.relative_time_position = string_or_empty(g_refs.relative_time_position);
    g_state_view.absolute_time_position = string_or_empty(g_refs.absolute_time_position);
    g_state_view.current_track = int_or_zero(g_refs.current_track);
    g_state_view.number_of_tracks = int_or_zero(g_refs.number_of_tracks);
    g_state_view.relative_counter_position = int_or_zero(g_refs.relative_counter_position);
    g_state_view.absolute_counter_position = int_or_zero(g_refs.absolute_counter_position);
    g_state_view.volume = int_or_zero(g_refs.volume);
    g_state_view.mute = bool_or_false(g_refs.mute);
    g_state_view.a_arg_type_direction = string_or_empty(g_refs.a_arg_type_direction);
    g_state_view.playback_storage_medium = string_or_empty(g_refs.playback_storage_medium);
    g_state_view.source_protocol_info = string_or_empty(g_refs.source_protocol_info);
    g_state_view.sink_protocol_info = string_or_empty(g_refs.sink_protocol_info);
    g_state_view.current_connection_ids = string_or_empty(g_refs.current_connection_ids);
}

static bool add_allowed_value(DlnaStateVariable *variable, const char *value)
{
    char **next_items;
    char *copy;

    if (!variable || !value)
        return false;

    copy = strdup(value);
    if (!copy)
        return false;

    next_items = realloc(variable->owned_allowed_values,
                         (variable->view.allowed_value_count + 1) * sizeof(*next_items));
    if (!next_items)
    {
        free(copy);
        return false;
    }

    variable->owned_allowed_values = next_items;
    variable->owned_allowed_values[variable->view.allowed_value_count] = copy;
    variable->view.allowed_values = (const char *const *)variable->owned_allowed_values;
    variable->view.allowed_value_count += 1;
    return true;
}

static bool add_state_variable(DlnaProtocolService service,
                               const char *name,
                               bool send_events,
                               DlnaStateDataType datatype,
                               const char *default_value,
                               const char *allowed_block,
                               int minimum,
                               int maximum,
                               int step,
                               bool has_range)
{
    DlnaStateVariable *existing;
    DlnaStateVariable *variable;

    if (!name || name[0] == '\0')
        return false;

    existing = find_state_variable_mutable(name);
    if (existing)
    {
        log_debug("[protocol-state] skip duplicate variable=%s service=%d\n", name, service);
        return true;
    }

    if (!ensure_state_capacity(g_state_store.count + 1))
        return false;

    variable = &g_state_store.items[g_state_store.count];
    memset(variable, 0, sizeof(*variable));
    variable->view.name = strdup(name);
    if (!variable->view.name)
        return false;

    variable->view.service = service;
    variable->view.send_events = send_events;
    variable->view.datatype = datatype;
    variable->view.has_range = has_range;
    variable->view.minimum = minimum;
    variable->view.maximum = maximum;
    variable->view.step = step;

    if (allowed_block)
    {
        const char *cursor = allowed_block;
        while ((cursor = strstr(cursor, "<allowedValue>")) != NULL)
        {
            const char *value_start = cursor + strlen("<allowedValue>");
            const char *value_end = strstr(value_start, "</allowedValue>");
            char *allowed_value;

            if (!value_end)
                break;

            allowed_value = dup_range(value_start, value_end);
            if (!allowed_value)
                return false;
            trim_in_place(allowed_value);

            if (!add_allowed_value(variable, allowed_value))
            {
                free(allowed_value);
                return false;
            }

            if (datatype == DLNA_STATE_TYPE_STRING &&
                (!default_value || default_value[0] == '\0') &&
                strcmp(allowed_value, "NOT_IMPLEMENTED") == 0)
            {
                if (!set_state_string_value(variable, allowed_value))
                {
                    free(allowed_value);
                    return false;
                }
            }

            free(allowed_value);
            cursor = value_end + strlen("</allowedValue>");
        }
    }

    switch (datatype)
    {
    case DLNA_STATE_TYPE_BOOLEAN:
        set_state_bool_value(variable, parse_boolean_value(default_value));
        break;
    case DLNA_STATE_TYPE_I2:
    case DLNA_STATE_TYPE_UI2:
    case DLNA_STATE_TYPE_I4:
    case DLNA_STATE_TYPE_UI4:
        set_state_int_value(variable, parse_int_value(default_value));
        break;
    case DLNA_STATE_TYPE_STRING:
    default:
        if (!variable->view.string_value &&
            !set_state_string_value(variable, default_value ? default_value : ""))
        {
            return false;
        }
        break;
    }

    g_state_store.count += 1;
    return true;
}

static bool parse_state_variable_block(const char *block_start,
                                       const char *block_end,
                                       const char *tag_end,
                                       DlnaProtocolService service)
{
    bool send_events = false;
    char *name = NULL;
    char *datatype = NULL;
    char *default_value = NULL;
    char *allowed_block = NULL;
    char *minimum = NULL;
    char *maximum = NULL;
    char *step = NULL;
    bool has_range = false;
    bool ok = false;

    if (!extract_send_events_value(block_start, tag_end, &send_events))
        goto cleanup;
    if (!extract_tag_text_dup(tag_end, block_end, "name", &name))
        goto cleanup;
    if (!extract_tag_text_dup(tag_end, block_end, "dataType", &datatype))
        goto cleanup;

    (void)extract_tag_text_dup(tag_end, block_end, "defaultValue", &default_value);

    const char *allowed_list = strstr(tag_end, "<allowedValueList>");
    if (allowed_list && allowed_list < block_end)
    {
        const char *allowed_end = strstr(allowed_list, "</allowedValueList>");
        if (allowed_end && allowed_end <= block_end)
            allowed_block = dup_range(allowed_list, allowed_end + strlen("</allowedValueList>"));
    }

    const char *range_block = strstr(tag_end, "<allowedValueRange>");
    if (range_block && range_block < block_end)
    {
        const char *range_end = strstr(range_block, "</allowedValueRange>");
        if (range_end && range_end <= block_end)
        {
            has_range = extract_tag_text_dup(range_block, range_end, "minimum", &minimum) &&
                        extract_tag_text_dup(range_block, range_end, "maximum", &maximum);
            (void)extract_tag_text_dup(range_block, range_end, "step", &step);
        }
    }

    ok = add_state_variable(service,
                            name,
                            send_events,
                            parse_datatype(datatype),
                            default_value,
                            allowed_block,
                            minimum ? parse_int_value(minimum) : 0,
                            maximum ? parse_int_value(maximum) : 0,
                            step ? parse_int_value(step) : 0,
                            has_range);

cleanup:
    free(name);
    free(datatype);
    free(default_value);
    free(allowed_block);
    free(minimum);
    free(maximum);
    free(step);
    return ok;
}

static bool load_service_state_variables(const char *template_path, DlnaProtocolService service)
{
    char *xml = NULL;
    const char *cursor;
    bool ok = true;

    if (!dlna_template_load_file_alloc(template_path, &xml, NULL))
        return false;

    cursor = xml;
    while ((cursor = strstr(cursor, "<stateVariable")) != NULL)
    {
        const char *tag_end = strchr(cursor, '>');
        const char *block_end;

        if (!tag_end)
        {
            ok = false;
            break;
        }

        block_end = strstr(tag_end, "</stateVariable>");
        if (!block_end)
        {
            ok = false;
            break;
        }

        if (!parse_state_variable_block(cursor, block_end, tag_end, service))
        {
            ok = false;
            break;
        }

        cursor = block_end + strlen("</stateVariable>");
    }

    free(xml);
    return ok;
}

static bool set_state_string_by_name(const char *name, const char *value)
{
    DlnaStateVariable *variable = find_state_variable_mutable(name);
    if (!variable)
        return false;
    if (!set_state_string_value(variable, value))
        return false;
    refresh_state_view();
    return true;
}

static void set_state_int_by_name(const char *name, int value)
{
    DlnaStateVariable *variable = find_state_variable_mutable(name);
    if (!variable)
        return;
    set_state_int_value(variable, value);
    refresh_state_view();
}

static void set_state_bool_by_name(const char *name, bool value)
{
    DlnaStateVariable *variable = find_state_variable_mutable(name);
    if (!variable)
        return;
    set_state_bool_value(variable, value);
    refresh_state_view();
}

static char *load_sink_protocol_info_csv(void)
{
    char *csv = NULL;

    if (!dlna_template_load_file_alloc("SinkProtocolInfo.csv", &csv, NULL))
        return strdup(DLNA_FALLBACK_SINK_PROTOCOL_INFO);

    trim_in_place(csv);
    return csv;
}

static void bind_state_refs(void)
{
    g_refs.transport_state = find_state_variable_mutable("TransportState");
    g_refs.transport_status = find_state_variable_mutable("TransportStatus");
    g_refs.transport_play_speed = find_state_variable_mutable("TransportPlaySpeed");
    g_refs.current_play_mode = find_state_variable_mutable("CurrentPlayMode");
    g_refs.av_transport_uri = find_state_variable_mutable("AVTransportURI");
    g_refs.av_transport_uri_metadata = find_state_variable_mutable("AVTransportURIMetaData");
    g_refs.next_av_transport_uri = find_state_variable_mutable("NextAVTransportURI");
    g_refs.next_av_transport_uri_metadata = find_state_variable_mutable("NextAVTransportURIMetaData");
    g_refs.current_track_uri = find_state_variable_mutable("CurrentTrackURI");
    g_refs.current_track_metadata = find_state_variable_mutable("CurrentTrackMetaData");
    g_refs.current_track_title = find_state_variable_mutable("CurrentTrackTitle");
    g_refs.current_media_duration = find_state_variable_mutable("CurrentMediaDuration");
    g_refs.current_track_duration = find_state_variable_mutable("CurrentTrackDuration");
    g_refs.relative_time_position = find_state_variable_mutable("RelativeTimePosition");
    g_refs.absolute_time_position = find_state_variable_mutable("AbsoluteTimePosition");
    g_refs.current_track = find_state_variable_mutable("CurrentTrack");
    g_refs.number_of_tracks = find_state_variable_mutable("NumberOfTracks");
    g_refs.relative_counter_position = find_state_variable_mutable("RelativeCounterPosition");
    g_refs.absolute_counter_position = find_state_variable_mutable("AbsoluteCounterPosition");
    g_refs.volume = find_state_variable_mutable("Volume");
    g_refs.mute = find_state_variable_mutable("Mute");
    g_refs.a_arg_type_direction = find_state_variable_mutable("A_ARG_TYPE_Direction");
    g_refs.playback_storage_medium = find_state_variable_mutable("PlaybackStorageMedium");
    g_refs.source_protocol_info = find_state_variable_mutable("SourceProtocolInfo");
    g_refs.sink_protocol_info = find_state_variable_mutable("SinkProtocolInfo");
    g_refs.current_connection_ids = find_state_variable_mutable("CurrentConnectionIDs");
}

static bool apply_macast_defaults(void)
{
    char *sink_protocol_info = load_sink_protocol_info_csv();

    if (!sink_protocol_info)
        return false;

    if (!set_state_string_by_name("CurrentPlayMode", "NORMAL") ||
        !set_state_string_by_name("TransportPlaySpeed", "1") ||
        !set_state_string_by_name("TransportStatus", "OK") ||
        !set_state_string_by_name("TransportState", "STOPPED") ||
        !set_state_string_by_name("CurrentMediaDuration", "00:00:00") ||
        !set_state_string_by_name("CurrentTrackDuration", "00:00:00") ||
        !set_state_string_by_name("RelativeTimePosition", "00:00:00") ||
        !set_state_string_by_name("AbsoluteTimePosition", "00:00:00") ||
        !set_state_string_by_name("A_ARG_TYPE_Direction", "Output") ||
        !set_state_string_by_name("CurrentConnectionIDs", "0") ||
        !set_state_string_by_name("PlaybackStorageMedium", "None") ||
        !set_state_string_by_name("SourceProtocolInfo", "") ||
        !set_state_string_by_name("SinkProtocolInfo", sink_protocol_info))
    {
        free(sink_protocol_info);
        return false;
    }

    free(sink_protocol_info);

    set_state_int_by_name("RelativeCounterPosition", DLNA_COUNTER_UNKNOWN);
    set_state_int_by_name("AbsoluteCounterPosition", DLNA_COUNTER_UNKNOWN);
    set_state_int_by_name("CurrentTrack", 0);
    set_state_int_by_name("NumberOfTracks", 0);
    set_state_int_by_name("Volume", PLAYER_DEFAULT_VOLUME);
    set_state_bool_by_name("Mute", false);
    return true;
}

static char *protocol_extract_title_alloc(const char *metadata)
{
    const char *cursor;

    if (!metadata)
        return NULL;

    cursor = metadata;
    while ((cursor = strchr(cursor, '<')) != NULL)
    {
        const char *name_start = cursor + 1;
        const char *name_end;
        const char *value_start;
        const char *close;
        const char *local_start;
        size_t local_len;
        size_t value_len;

        if (*name_start == '/' || *name_start == '?' || *name_start == '!')
        {
            ++cursor;
            continue;
        }

        name_end = name_start;
        while (*name_end && *name_end != '>' && *name_end != ' ' && *name_end != '\t' && *name_end != '/')
            ++name_end;
        if (name_end == name_start)
        {
            ++cursor;
            continue;
        }

        local_start = memchr(name_start, ':', (size_t)(name_end - name_start));
        if (local_start)
            ++local_start;
        else
            local_start = name_start;
        local_len = (size_t)(name_end - local_start);
        if (local_len != 5 || strncasecmp(local_start, "title", 5) != 0)
        {
            ++cursor;
            continue;
        }

        value_start = strchr(name_end, '>');
        if (!value_start)
            return false;
        ++value_start;
        close = strstr(value_start, "</");
        if (!close || close <= value_start)
            return false;

        value_len = (size_t)(close - value_start);
        char *title = malloc(value_len + 1);
        if (!title)
            return NULL;
        memcpy(title, value_start, value_len);
        title[value_len] = '\0';
        return title[0] != '\0' ? title : (free(title), NULL);
    }

    return NULL;
}

const char *dlna_protocol_transport_state_from_renderer_state(RendererState state)
{
    switch (state)
    {
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED_PLAYBACK";
    case PLAYER_STATE_LOADING:
    case PLAYER_STATE_BUFFERING:
    case PLAYER_STATE_SEEKING:
        return "TRANSITIONING";
    case PLAYER_STATE_IDLE:
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_ERROR:
    default:
        return "STOPPED";
    }
}

const char *dlna_protocol_transport_status_from_renderer_state(RendererState state)
{
    return state == PLAYER_STATE_ERROR ? "ERROR_OCCURRED" : "OK";
}

char *dlna_protocol_format_hhmmss_alloc(int value_ms)
{
    int total_seconds;
    int hour;
    int minute;
    int second;

    if (value_ms < 0)
        value_ms = 0;

    total_seconds = value_ms / 1000;
    hour = total_seconds / 3600;
    minute = (total_seconds % 3600) / 60;
    second = total_seconds % 60;
    return protocol_strdup_printf("%02d:%02d:%02d", hour, minute, second);
}

void dlna_protocol_state_init(void)
{
    clear_state_store();

    for (size_t i = 0; i < DLNA_SERVICE_XML_COUNT; ++i)
    {
        if (!load_service_state_variables(g_service_templates[i].template_path,
                                          g_service_templates[i].service))
        {
            log_error("[protocol-state] failed to load service state xml=%s\n",
                      g_service_templates[i].template_path);
            clear_state_store();
            return;
        }
    }

    bind_state_refs();
    if (!apply_macast_defaults())
    {
        log_error("[protocol-state] failed to apply defaults\n");
        clear_state_store();
        return;
    }

    refresh_state_view();
    g_state_initialized = true;
}

void dlna_protocol_state_reset(void)
{
    clear_state_store();
}

const DlnaProtocolStateView *dlna_protocol_state_view(void)
{
    return &g_state_view;
}

size_t dlna_protocol_state_variable_count(void)
{
    return g_state_store.count;
}

const DlnaStateVariableView *dlna_protocol_state_variable_at(size_t index)
{
    if (index >= g_state_store.count)
        return NULL;
    return &g_state_store.items[index].view;
}

const DlnaStateVariableView *dlna_protocol_state_find_variable(const char *name)
{
    const DlnaStateVariable *variable = find_state_variable_const_internal(name);
    return variable ? &variable->view : NULL;
}

void dlna_protocol_state_apply_set_uri(const char *uri, const char *metadata)
{
    char *title = NULL;

    if (!g_state_initialized)
        return;

    set_state_string_by_name("AVTransportURI", uri);
    set_state_string_by_name("AVTransportURIMetaData", metadata);
    set_state_string_by_name("CurrentTrackURI", uri);
    set_state_string_by_name("CurrentTrackMetaData", metadata);

    title = protocol_extract_title_alloc(metadata);
    if (title)
    {
        set_state_string_by_name("CurrentTrackTitle", title);
        free(title);
    }
    else
        set_state_string_by_name("CurrentTrackTitle", "");

    set_state_int_by_name("CurrentTrack", uri && uri[0] != '\0' ? 1 : 0);
    set_state_int_by_name("NumberOfTracks", uri && uri[0] != '\0' ? 1 : 0);
    set_state_string_by_name("RelativeTimePosition", "00:00:00");
    set_state_string_by_name("AbsoluteTimePosition", "00:00:00");
    set_state_string_by_name("TransportState", "PAUSED_PLAYBACK");
    set_state_string_by_name("TransportStatus", "OK");
}

void dlna_protocol_state_apply_play(void)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("TransportState", "PLAYING");
    set_state_string_by_name("TransportStatus", "OK");
}

void dlna_protocol_state_apply_pause(void)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("TransportState", "PAUSED_PLAYBACK");
    set_state_string_by_name("TransportStatus", "OK");
}

void dlna_protocol_state_apply_stop(void)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("TransportState", "STOPPED");
    set_state_string_by_name("TransportStatus", "OK");
}

void dlna_protocol_state_apply_seek_target(const char *target)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("RelativeTimePosition", target ? target : "00:00:00");
    set_state_string_by_name("AbsoluteTimePosition", target ? target : "00:00:00");
}

void dlna_protocol_state_set_transport_status(const char *status)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("TransportStatus", status);
}

void dlna_protocol_state_set_transport_speed(const char *speed)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("TransportPlaySpeed", speed);
}

void dlna_protocol_state_set_transport_timing(int duration_ms, int position_ms)
{
    char *duration = NULL;
    char *position = NULL;

    if (!g_state_initialized)
        return;

    duration = dlna_protocol_format_hhmmss_alloc(duration_ms);
    position = dlna_protocol_format_hhmmss_alloc(position_ms);
    if (!duration || !position)
    {
        free(duration);
        free(position);
        return;
    }

    set_state_string_by_name("CurrentMediaDuration", duration);
    set_state_string_by_name("CurrentTrackDuration", duration);
    set_state_string_by_name("RelativeTimePosition", position);
    set_state_string_by_name("AbsoluteTimePosition", position);
    free(duration);
    free(position);
}

void dlna_protocol_state_set_source_protocol_info(const char *value)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("SourceProtocolInfo", value);
}

void dlna_protocol_state_set_sink_protocol_info(const char *value)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("SinkProtocolInfo", value);
}

void dlna_protocol_state_set_connection_ids(const char *value)
{
    if (!g_state_initialized)
        return;
    set_state_string_by_name("CurrentConnectionIDs", value);
}

void dlna_protocol_state_sync_from_renderer(void)
{
    RendererSnapshot snapshot;

    if (!g_state_initialized || !renderer_get_snapshot(&snapshot))
        return;

    dlna_protocol_state_set_transport_timing(snapshot.duration_ms, snapshot.position_ms);
    set_state_int_by_name("Volume", snapshot.volume);
    set_state_bool_by_name("Mute", snapshot.mute);

    if (!snapshot.has_media)
    {
        renderer_snapshot_clear(&snapshot);
        return;
    }

    if (should_apply_transport_state_from_renderer(snapshot.state))
    {
        set_state_string_by_name("TransportState",
                                 dlna_protocol_transport_state_from_renderer_state(snapshot.state));
        set_state_string_by_name("TransportStatus",
                                 dlna_protocol_transport_status_from_renderer_state(snapshot.state));
    }
    renderer_snapshot_clear(&snapshot);
}

void dlna_protocol_state_on_renderer_event(const RendererEvent *event)
{
    char *hhmmss = NULL;

    if (!g_state_initialized || !event)
        return;

    switch (event->type)
    {
    case PLAYER_EVENT_STATE_CHANGED:
        if (should_apply_transport_state_from_renderer(event->state))
        {
            set_state_string_by_name("TransportState",
                                     dlna_protocol_transport_state_from_renderer_state(event->state));
            set_state_string_by_name("TransportStatus",
                                     dlna_protocol_transport_status_from_renderer_state(event->state));
        }
        break;
    case PLAYER_EVENT_POSITION_CHANGED:
        hhmmss = dlna_protocol_format_hhmmss_alloc(event->position_ms);
        if (!hhmmss)
            break;
        set_state_string_by_name("RelativeTimePosition", hhmmss);
        set_state_string_by_name("AbsoluteTimePosition", hhmmss);
        free(hhmmss);
        break;
    case PLAYER_EVENT_DURATION_CHANGED:
        hhmmss = dlna_protocol_format_hhmmss_alloc(event->duration_ms);
        if (!hhmmss)
            break;
        set_state_string_by_name("CurrentMediaDuration", hhmmss);
        set_state_string_by_name("CurrentTrackDuration", hhmmss);
        free(hhmmss);
        break;
    case PLAYER_EVENT_VOLUME_CHANGED:
        set_state_int_by_name("Volume", event->volume);
        break;
    case PLAYER_EVENT_MUTE_CHANGED:
        set_state_bool_by_name("Mute", event->mute);
        break;
    case PLAYER_EVENT_URI_CHANGED:
        set_state_string_by_name("CurrentTrackURI", event->uri);
        break;
    case PLAYER_EVENT_ERROR:
        set_state_string_by_name("TransportState", "STOPPED");
        set_state_string_by_name("TransportStatus", "ERROR_OCCURRED");
        break;
    default:
        break;
    }
}
