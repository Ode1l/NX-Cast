#include "player_backend.h"

#include <switch.h>

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

#ifdef HAVE_LIBMPV
#include <mpv/client.h>

#define PLAYER_LIBMPV_URI_MAX 1024
#define PLAYER_LIBMPV_METADATA_MAX 2048
#define PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS "10"
#define PLAYER_LIBMPV_DEMUXER_READAHEAD_SECONDS "8"
#define PLAYER_LIBMPV_HLS_LOG_LEVEL "warn"
#define PLAYER_LIBMPV_DEFAULT_LOG_LEVEL "warn"
#define PLAYER_LIBMPV_HLS_DIAG_INTERVAL_MS 2000
#define PLAYER_LIBMPV_DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"
static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static Mutex g_state_mutex;
static bool g_mutex_ready = false;
static mpv_handle *g_mpv = NULL;
static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = 20;
static bool g_mute = false;
static bool g_media_loaded = false;
static bool g_load_in_progress = false;
static bool g_seekable = false;
static bool g_core_idle = true;
static bool g_paused_for_cache = false;
static bool g_seeking = false;
static bool g_playback_abort = false;
static bool g_stop_mode = true;
static bool g_hls_mode = false;
static bool g_has_source = false;
static bool g_last_diag_valid = false;
static PlayerState g_last_diag_state = PLAYER_STATE_IDLE;
static bool g_last_diag_media_loaded = false;
static bool g_last_diag_load_in_progress = false;
static bool g_last_diag_seekable = false;
static bool g_last_diag_core_idle = true;
static bool g_last_diag_paused_for_cache = false;
static bool g_last_diag_seeking = false;
static bool g_last_diag_playback_abort = false;
static int g_cache_duration_ms = 0;
static int64_t g_cache_speed_bps = 0;
static uint64_t g_last_diag_log_ms = 0;
static uint64_t g_load_started_ms = 0;
static PlayerResolvedSource g_source;
static char g_uri[PLAYER_LIBMPV_URI_MAX];
static char g_metadata[PLAYER_LIBMPV_METADATA_MAX];

static void libmpv_clip_for_log(const char *input, char *output, size_t output_size);
static uint64_t libmpv_now_ms(void);
static void libmpv_log_message(const mpv_event_log_message *message);
static const char *libmpv_state_name(PlayerState state);
static int libmpv_ms_from_seconds(double seconds);
static bool libmpv_get_int64_locked(const char *name, int64_t *out);
static bool libmpv_get_string_locked(const char *name, char *out, size_t out_size);
static void libmpv_request_log_level_locked(const char *level);
static bool libmpv_set_string_property_locked(const char *name, const char *value);
static void libmpv_apply_source_runtime_overrides_locked(const PlayerResolvedSource *source);
static void libmpv_log_cache_state_node(const mpv_node *node);
static void libmpv_log_stream_details_locked(const char *reason);
static void libmpv_maybe_log_diagnostics_locked(const char *reason, bool force);

enum
{
    LIBMPV_OBS_CORE_IDLE = 1,
    LIBMPV_OBS_EOF_REACHED,
    LIBMPV_OBS_DURATION,
    LIBMPV_OBS_PLAYBACK_TIME,
    LIBMPV_OBS_CACHE_SPEED,
    LIBMPV_OBS_DEMUXER_CACHE_DURATION,
    LIBMPV_OBS_PAUSED_FOR_CACHE,
    LIBMPV_OBS_DEMUXER_CACHE_STATE,
    LIBMPV_OBS_PAUSE,
    LIBMPV_OBS_PLAYBACK_ABORT,
    LIBMPV_OBS_SEEKING,
    LIBMPV_OBS_SEEKABLE
};

static const char *libmpv_end_reason_name(mpv_end_file_reason reason)
{
    switch (reason)
    {
    case MPV_END_FILE_REASON_EOF:
        return "eof";
    case MPV_END_FILE_REASON_STOP:
        return "stop";
    case MPV_END_FILE_REASON_QUIT:
        return "quit";
    case MPV_END_FILE_REASON_ERROR:
        return "error";
    case MPV_END_FILE_REASON_REDIRECT:
        return "redirect";
    default:
        return "unknown";
    }
}

static void libmpv_drain_events_locked(void)
{
    if (!g_mpv)
        return;

    for (;;)
    {
        mpv_event *event = mpv_wait_event(g_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id)
        {
        case MPV_EVENT_START_FILE:
            g_media_loaded = false;
            g_load_in_progress = true;
            g_seekable = false;
            g_load_started_ms = libmpv_now_ms();
            log_debug("[player-libmpv] event START_FILE\n");
            libmpv_maybe_log_diagnostics_locked("START_FILE", true);
            break;
        case MPV_EVENT_FILE_LOADED:
            g_media_loaded = true;
            g_load_in_progress = false;
            log_info("[player-libmpv] event FILE_LOADED load_elapsed_ms=%llu\n",
                     (unsigned long long)(g_load_started_ms == 0 ? 0 : (libmpv_now_ms() - g_load_started_ms)));
            libmpv_maybe_log_diagnostics_locked("FILE_LOADED", true);
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            log_info("[player-libmpv] event PLAYBACK_RESTART startup_elapsed_ms=%llu\n",
                     (unsigned long long)(g_load_started_ms == 0 ? 0 : (libmpv_now_ms() - g_load_started_ms)));
            libmpv_maybe_log_diagnostics_locked("PLAYBACK_RESTART", true);
            break;
        case MPV_EVENT_END_FILE:
        {
            const mpv_event_end_file *end = (const mpv_event_end_file *)event->data;
            g_media_loaded = false;
            g_load_in_progress = false;
            g_seekable = false;
            g_paused_for_cache = false;
            g_seeking = false;
            g_position_ms = 0;
            if (end)
            {
                const char *reason = libmpv_end_reason_name(end->reason);
                const char *error = end->error != 0 ? mpv_error_string(end->error) : "none";
                if (end->reason == MPV_END_FILE_REASON_ERROR)
                    log_error("[player-libmpv] event END_FILE reason=%s error=%s\n", reason, error);
                else
                    log_info("[player-libmpv] event END_FILE reason=%s error=%s\n", reason, error);
                if (end->reason == MPV_END_FILE_REASON_ERROR)
                    g_state = PLAYER_STATE_ERROR;
            }
            else
            {
                log_info("[player-libmpv] event END_FILE\n");
            }
            if (g_uri[0] != '\0')
                g_stop_mode = true;
            libmpv_maybe_log_diagnostics_locked("END_FILE", true);
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE:
        {
            const mpv_event_property *property = (const mpv_event_property *)event->data;
            if (!property || !property->data)
                break;

            switch (event->reply_userdata)
            {
            case LIBMPV_OBS_CORE_IDLE:
                if (property->format == MPV_FORMAT_FLAG)
                    g_core_idle = (*(int *)property->data) != 0;
                break;
            case LIBMPV_OBS_EOF_REACHED:
                if (property->format == MPV_FORMAT_FLAG && (*(int *)property->data) != 0)
                {
                    g_stop_mode = true;
                    g_media_loaded = false;
                    g_seekable = false;
                }
                break;
            case LIBMPV_OBS_DURATION:
                if (property->format == MPV_FORMAT_INT64)
                    g_duration_ms = (int)(*(int64_t *)property->data * 1000);
                else if (property->format == MPV_FORMAT_DOUBLE)
                    g_duration_ms = libmpv_ms_from_seconds(*(double *)property->data);
                break;
            case LIBMPV_OBS_PLAYBACK_TIME:
                if (property->format == MPV_FORMAT_DOUBLE)
                    g_position_ms = libmpv_ms_from_seconds(*(double *)property->data);
                break;
            case LIBMPV_OBS_CACHE_SPEED:
                if (property->format == MPV_FORMAT_INT64)
                    g_cache_speed_bps = *(int64_t *)property->data;
                break;
            case LIBMPV_OBS_DEMUXER_CACHE_DURATION:
                if (property->format == MPV_FORMAT_DOUBLE)
                    g_cache_duration_ms = libmpv_ms_from_seconds(*(double *)property->data);
                break;
            case LIBMPV_OBS_PAUSED_FOR_CACHE:
                if (property->format == MPV_FORMAT_FLAG)
                    g_paused_for_cache = (*(int *)property->data) != 0;
                break;
            case LIBMPV_OBS_DEMUXER_CACHE_STATE:
                if (property->format == MPV_FORMAT_NODE && g_hls_mode)
                    libmpv_log_cache_state_node((const mpv_node *)property->data);
                break;
            case LIBMPV_OBS_PAUSE:
                break;
            case LIBMPV_OBS_PLAYBACK_ABORT:
                if (property->format == MPV_FORMAT_FLAG)
                    g_playback_abort = (*(int *)property->data) != 0;
                break;
            case LIBMPV_OBS_SEEKING:
                if (property->format == MPV_FORMAT_FLAG)
                    g_seeking = (*(int *)property->data) != 0;
                break;
            case LIBMPV_OBS_SEEKABLE:
                if (property->format == MPV_FORMAT_FLAG)
                    g_seekable = g_media_loaded && ((*(int *)property->data) != 0);
                break;
            default:
                break;
            }
            break;
        }
        case MPV_EVENT_LOG_MESSAGE:
        {
            const mpv_event_log_message *message = (const mpv_event_log_message *)event->data;
            libmpv_log_message(message);
            break;
        }
        default:
            break;
        }
    }
}

static void libmpv_ensure_mutex(void)
{
    if (g_mutex_ready)
        return;
    mutexInit(&g_state_mutex);
    g_mutex_ready = true;
}

static void libmpv_clip_for_log(const char *input, char *output, size_t output_size)
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

static uint64_t libmpv_now_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static const char *libmpv_state_name(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_IDLE:
        return "IDLE";
    case PLAYER_STATE_STOPPED:
        return "STOPPED";
    case PLAYER_STATE_LOADING:
        return "LOADING";
    case PLAYER_STATE_BUFFERING:
        return "BUFFERING";
    case PLAYER_STATE_SEEKING:
        return "SEEKING";
    case PLAYER_STATE_PLAYING:
        return "PLAYING";
    case PLAYER_STATE_PAUSED:
        return "PAUSED";
    case PLAYER_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static void libmpv_log_message(const mpv_event_log_message *message)
{
    if (!message || !message->text)
        return;

    char clipped[192];
    libmpv_clip_for_log(message->text, clipped, sizeof(clipped));

    const char *prefix = message->prefix ? message->prefix : "?";
    const char *level = message->level ? message->level : "?";

    if (message->log_level <= MPV_LOG_LEVEL_ERROR)
    {
        log_error("[player-libmpv] mpv[%s/%s] %s\n", prefix, level, clipped);
        return;
    }

    if (message->log_level <= MPV_LOG_LEVEL_WARN)
    {
        log_warn("[player-libmpv] mpv[%s/%s] %s\n", prefix, level, clipped);
        return;
    }

    if (message->log_level <= MPV_LOG_LEVEL_INFO)
    {
        log_info("[player-libmpv] mpv[%s/%s] %s\n", prefix, level, clipped);
        return;
    }

    log_debug("[player-libmpv] mpv[%s/%s] %s\n", prefix, level, clipped);
}

static int libmpv_ms_from_seconds(double seconds)
{
    if (!(seconds >= 0.0))
        return 0;

    double ms = seconds * 1000.0;
    if (ms >= (double)INT_MAX)
        return INT_MAX;
    return (int)(ms + 0.5);
}

static void libmpv_emit_event_locked(PlayerEventType type)
{
    if (!g_event_sink)
        return;

    PlayerEvent event = {
        .type = type,
        .state = g_state,
        .position_ms = g_position_ms,
        .duration_ms = g_duration_ms,
        .volume = g_volume,
        .mute = g_mute,
        .error_code = 0,
        .uri = g_uri,
        .source_profile = g_has_source ? g_source.profile : PLAYER_SOURCE_PROFILE_UNKNOWN
    };
    g_event_sink(&event);
}

static bool libmpv_get_flag_locked(const char *name, bool *out)
{
    int value = 0;
    int rc = mpv_get_property(g_mpv, name, MPV_FORMAT_FLAG, &value);
    if (rc < 0)
        return false;

    if (out)
        *out = value != 0;
    return true;
}

static bool libmpv_get_double_locked(const char *name, double *out)
{
    double value = 0.0;
    int rc = mpv_get_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value);
    if (rc < 0)
        return false;

    if (out)
        *out = value;
    return true;
}

static bool libmpv_get_int64_locked(const char *name, int64_t *out)
{
    int64_t value = 0;
    int rc = mpv_get_property(g_mpv, name, MPV_FORMAT_INT64, &value);
    if (rc < 0)
        return false;

    if (out)
        *out = value;
    return true;
}

static bool libmpv_get_string_locked(const char *name, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;

    char *value = mpv_get_property_string(g_mpv, name);
    if (!value)
        return false;

    snprintf(out, out_size, "%s", value);
    mpv_free(value);
    return true;
}

static void libmpv_request_log_level_locked(const char *level)
{
    if (!g_mpv || !level)
        return;

    int rc = mpv_request_log_messages(g_mpv, level);
    if (rc < 0)
        log_warn("[player-libmpv] request_log_messages level=%s failed: %s\n",
                 level,
                 mpv_error_string(rc));
}

static bool libmpv_set_string_property_locked(const char *name, const char *value)
{
    int rc;

    if (!g_mpv || !name)
        return false;

    rc = mpv_set_property_string(g_mpv, name, value ? value : "");
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_property %s=%s failed: %s\n",
                 name,
                 value ? value : "<empty>",
                 mpv_error_string(rc));
        return false;
    }

    return true;
}

static bool libmpv_set_flag_locked(const char *name, bool value)
{
    int flag = value ? 1 : 0;
    int rc = mpv_set_property(g_mpv, name, MPV_FORMAT_FLAG, &flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_property %s=%d failed: %s\n",
                 name,
                 flag,
                 mpv_error_string(rc));
        return false;
    }
    return true;
}

static bool libmpv_set_double_locked(const char *name, double value)
{
    int rc = mpv_set_property(g_mpv, name, MPV_FORMAT_DOUBLE, &value);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_property %s=%.3f failed: %s\n",
                 name,
                 value,
                 mpv_error_string(rc));
        return false;
    }
    return true;
}

static bool libmpv_command_locked(const char *action, const char **args)
{
    int rc = mpv_command(g_mpv, args);
    if (rc < 0)
    {
        log_warn("[player-libmpv] command %s failed: %s\n", action, mpv_error_string(rc));
        return false;
    }
    return true;
}

static void libmpv_log_cache_state_node(const mpv_node *node)
{
    if (!node || node->format != MPV_FORMAT_NODE_MAP || !node->u.list)
        return;

    const mpv_node_list *list = node->u.list;
    int64_t total_bytes = 0;
    int64_t fw_bytes = 0;
    int64_t raw_input_rate = 0;
    double cache_duration = 0.0;
    bool underrun = false;
    bool bof_cached = false;
    bool eof_cached = false;

    for (int i = 0; i < list->num; ++i)
    {
        const char *key = list->keys ? list->keys[i] : NULL;
        const mpv_node *value = &list->values[i];
        if (!key || !value)
            continue;

        if (strcmp(key, "total-bytes") == 0 && value->format == MPV_FORMAT_INT64)
            total_bytes = value->u.int64;
        else if (strcmp(key, "fw-bytes") == 0 && value->format == MPV_FORMAT_INT64)
            fw_bytes = value->u.int64;
        else if (strcmp(key, "raw-input-rate") == 0 && value->format == MPV_FORMAT_INT64)
            raw_input_rate = value->u.int64;
        else if (strcmp(key, "cache-duration") == 0 && value->format == MPV_FORMAT_DOUBLE)
            cache_duration = value->u.double_;
        else if (strcmp(key, "underrun") == 0 && value->format == MPV_FORMAT_FLAG)
            underrun = value->u.flag != 0;
        else if (strcmp(key, "bof-cached") == 0 && value->format == MPV_FORMAT_FLAG)
            bof_cached = value->u.flag != 0;
        else if (strcmp(key, "eof-cached") == 0 && value->format == MPV_FORMAT_FLAG)
            eof_cached = value->u.flag != 0;
    }

    log_info("[player-libmpv] cache_state total_mb=%.2f fw_mb=%.2f cache_sec=%.2f raw_rate_bps=%lld underrun=%d bof=%d eof=%d\n",
             total_bytes / 1048576.0,
             fw_bytes / 1048576.0,
             cache_duration,
             (long long)raw_input_rate,
             underrun ? 1 : 0,
             bof_cached ? 1 : 0,
             eof_cached ? 1 : 0);
}

static void libmpv_maybe_log_diagnostics_locked(const char *reason, bool force)
{
    bool changed = force || !g_last_diag_valid;
    uint64_t now_ms = armTicksToNs(armGetSystemTick()) / 1000000ULL;

    if (!changed)
    {
        changed = g_state != g_last_diag_state ||
                  g_media_loaded != g_last_diag_media_loaded ||
                  g_load_in_progress != g_last_diag_load_in_progress ||
                  g_seekable != g_last_diag_seekable ||
                  g_core_idle != g_last_diag_core_idle ||
                  g_paused_for_cache != g_last_diag_paused_for_cache ||
                  g_seeking != g_last_diag_seeking ||
                  g_playback_abort != g_last_diag_playback_abort;
    }

    if (!changed && g_hls_mode &&
        (g_state == PLAYER_STATE_LOADING || g_state == PLAYER_STATE_BUFFERING || g_state == PLAYER_STATE_SEEKING) &&
        now_ms - g_last_diag_log_ms >= PLAYER_LIBMPV_HLS_DIAG_INTERVAL_MS)
    {
        changed = true;
    }

    if (!changed)
        return;

    char file_format[64];
    char current_demuxer[64];
    bool via_network = false;
    file_format[0] = '\0';
    current_demuxer[0] = '\0';
    if (!libmpv_get_string_locked("file-format", file_format, sizeof(file_format)))
        snprintf(file_format, sizeof(file_format), "%s", "?");
    if (!libmpv_get_string_locked("current-demuxer", current_demuxer, sizeof(current_demuxer)))
        snprintf(current_demuxer, sizeof(current_demuxer), "%s", "?");
    (void)libmpv_get_flag_locked("demuxer-via-network", &via_network);

    log_info("[player-libmpv] diag reason=%s uri_kind=%s state=%s loaded=%d loading=%d buffering=%d seeking=%d stop_mode=%d core_idle=%d abort=%d seekable=%d via_net=%d pos_ms=%d dur_ms=%d cache_ms=%d cache_speed_bps=%lld file_format=%s demuxer=%s\n",
             reason ? reason : "refresh",
             g_hls_mode ? "hls" : "default",
             libmpv_state_name(g_state),
             g_media_loaded ? 1 : 0,
             g_load_in_progress ? 1 : 0,
             g_paused_for_cache ? 1 : 0,
             g_seeking ? 1 : 0,
             g_stop_mode ? 1 : 0,
             g_core_idle ? 1 : 0,
             g_playback_abort ? 1 : 0,
             g_seekable ? 1 : 0,
             via_network ? 1 : 0,
             g_position_ms,
             g_duration_ms,
             g_cache_duration_ms,
             (long long)g_cache_speed_bps,
             file_format,
             current_demuxer);

    if (g_hls_mode)
        libmpv_log_stream_details_locked(reason);

    g_last_diag_valid = true;
    g_last_diag_state = g_state;
    g_last_diag_media_loaded = g_media_loaded;
    g_last_diag_load_in_progress = g_load_in_progress;
    g_last_diag_seekable = g_seekable;
    g_last_diag_core_idle = g_core_idle;
    g_last_diag_paused_for_cache = g_paused_for_cache;
    g_last_diag_seeking = g_seeking;
    g_last_diag_playback_abort = g_playback_abort;
    g_last_diag_log_ms = now_ms;
}

static void libmpv_log_stream_details_locked(const char *reason)
{
    char stream_open[192];
    char stream_path[192];
    char path[192];
    char demuxer[64];
    char clipped_open[144];
    char clipped_stream_path[144];
    char clipped_path[144];
    bool via_network = false;

    stream_open[0] = '\0';
    stream_path[0] = '\0';
    path[0] = '\0';
    demuxer[0] = '\0';
    clipped_open[0] = '\0';
    clipped_stream_path[0] = '\0';
    clipped_path[0] = '\0';

    if (!libmpv_get_string_locked("stream-open-filename", stream_open, sizeof(stream_open)))
        snprintf(stream_open, sizeof(stream_open), "%s", "?");
    if (!libmpv_get_string_locked("stream-path", stream_path, sizeof(stream_path)))
        snprintf(stream_path, sizeof(stream_path), "%s", "?");
    if (!libmpv_get_string_locked("path", path, sizeof(path)))
        snprintf(path, sizeof(path), "%s", "?");
    if (!libmpv_get_string_locked("current-demuxer", demuxer, sizeof(demuxer)))
        snprintf(demuxer, sizeof(demuxer), "%s", "?");
    (void)libmpv_get_flag_locked("demuxer-via-network", &via_network);

    libmpv_clip_for_log(stream_open, clipped_open, sizeof(clipped_open));
    libmpv_clip_for_log(stream_path, clipped_stream_path, sizeof(clipped_stream_path));
    libmpv_clip_for_log(path, clipped_path, sizeof(clipped_path));

    log_info("[player-libmpv] diag detail reason=%s demuxer=%s via_net=%d stream_open=%s stream_path=%s path=%s\n",
             reason ? reason : "refresh",
             demuxer,
             via_network ? 1 : 0,
             clipped_open,
             clipped_stream_path,
             clipped_path);
}

static void libmpv_apply_source_runtime_overrides_locked(const PlayerResolvedSource *source)
{
    char network_timeout[16];

    if (!source)
        return;

    snprintf(network_timeout,
             sizeof(network_timeout),
             "%d",
             source->network_timeout_seconds > 0 ? source->network_timeout_seconds : 10);
    (void)libmpv_set_string_property_locked("user-agent", source->user_agent);
    (void)libmpv_set_string_property_locked("referrer", source->referrer);
    (void)libmpv_set_string_property_locked("network-timeout", network_timeout);
    (void)libmpv_set_string_property_locked("http-header-fields", source->header_fields);
    (void)libmpv_set_string_property_locked("demuxer-lavf-probe-info", source->probe_info);

    log_info("[player-libmpv] runtime_overrides profile=%s hls=%d signed=%d bilibili=%d timeout=%s probe_info=%s headers=%d\n",
             player_source_profile_name(source->profile),
             source->flags.is_hls ? 1 : 0,
             source->flags.is_signed ? 1 : 0,
             source->flags.is_bilibili ? 1 : 0,
             network_timeout,
             source->probe_info[0] != '\0' ? source->probe_info : "auto",
             source->header_fields[0] != '\0' ? 1 : 0);
}

static bool libmpv_load_uri_locked(const char *uri, bool paused)
{
    char options[256];
    const char *cmd[8];
    size_t index = 0;

    snprintf(options, sizeof(options), "pause=%s", paused ? "yes" : "no");

    cmd[index++] = "loadfile";
    cmd[index++] = uri;
    cmd[index++] = "replace";
    cmd[index++] = "0";
    cmd[index++] = options;
    cmd[index] = NULL;

    // mpv 0.38+ expects the optional playlist index placeholder before the
    // per-file options string. Site-specific options must be combined into
    // that single options argument rather than passed as extra argv items.
    return libmpv_command_locked(paused ? "loadfile(pause=yes)" : "loadfile(pause=no)", cmd);
}

static bool libmpv_seek_absolute_locked(int position_ms)
{
    char seconds_buffer[32];
    double seconds = (double)position_ms / 1000.0;
    snprintf(seconds_buffer, sizeof(seconds_buffer), "%.3f", seconds);

    const char *cmd[] = {"seek", seconds_buffer, "absolute", "exact", NULL};
    return libmpv_command_locked("seek", cmd);
}

static bool libmpv_stop_command_locked(void)
{
    const char *cmd[] = {"stop", NULL};
    return libmpv_command_locked("stop", cmd);
}

static void libmpv_refresh_snapshot_locked(void)
{
    if (!g_mpv)
        return;

    libmpv_drain_events_locked();

    bool paused = true;
    bool idle_active = false;
    bool eof_reached = false;
    bool seekable = false;
    bool paused_for_cache = false;
    bool seeking = false;
    bool playback_abort = false;
    double playback_seconds = 0.0;
    double duration_seconds = 0.0;
    double cache_duration_seconds = 0.0;
    double volume = 0.0;
    bool mute = false;
    int64_t cache_speed = 0;

    if (libmpv_get_flag_locked("pause", &paused))
    {
        // keep local paused value only when property is available
    }
    libmpv_get_flag_locked("idle-active", &idle_active);
    if (libmpv_get_flag_locked("paused-for-cache", &paused_for_cache))
        g_paused_for_cache = paused_for_cache;
    if (libmpv_get_flag_locked("seeking", &seeking))
        g_seeking = seeking;
    if (libmpv_get_flag_locked("playback-abort", &playback_abort))
        g_playback_abort = playback_abort;
    g_core_idle = idle_active;

    if (libmpv_get_flag_locked("eof-reached", &eof_reached) && eof_reached)
    {
        g_stop_mode = true;
        g_media_loaded = false;
        g_seekable = false;
    }

    if (libmpv_get_flag_locked("seekable", &seekable))
        g_seekable = g_media_loaded && seekable;
    else if (!g_media_loaded)
        g_seekable = false;

    if (libmpv_get_double_locked("playback-time", &playback_seconds))
        g_position_ms = libmpv_ms_from_seconds(playback_seconds);
    else if (g_stop_mode)
        g_position_ms = 0;

    if (libmpv_get_double_locked("duration", &duration_seconds))
        g_duration_ms = libmpv_ms_from_seconds(duration_seconds);
    if (libmpv_get_double_locked("demuxer-cache-duration", &cache_duration_seconds))
        g_cache_duration_ms = libmpv_ms_from_seconds(cache_duration_seconds);
    if (libmpv_get_int64_locked("cache-speed", &cache_speed))
        g_cache_speed_bps = cache_speed;

    if (libmpv_get_double_locked("volume", &volume))
    {
        if (volume < 0.0)
            volume = 0.0;
        if (volume > 100.0)
            volume = 100.0;
        g_volume = (int)(volume + 0.5);
    }

    if (libmpv_get_flag_locked("mute", &mute))
        g_mute = mute;

    if (g_uri[0] == '\0')
    {
        g_state = PLAYER_STATE_STOPPED;
        return;
    }

    if (g_state == PLAYER_STATE_ERROR)
    {
        libmpv_maybe_log_diagnostics_locked("refresh(error)", false);
        return;
    }

    if (g_load_in_progress)
        g_state = PLAYER_STATE_LOADING;
    else if (g_seeking)
        g_state = PLAYER_STATE_SEEKING;
    else if (g_paused_for_cache)
        g_state = PLAYER_STATE_BUFFERING;
    else if (g_stop_mode || (!g_media_loaded && idle_active))
        g_state = PLAYER_STATE_STOPPED;
    else if (paused)
        g_state = PLAYER_STATE_PAUSED;
    else
        g_state = PLAYER_STATE_PLAYING;

    libmpv_maybe_log_diagnostics_locked("refresh", false);
}

static bool libmpv_available(void)
{
    return true;
}

static bool libmpv_init(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = 20;
    g_mute = false;
    g_media_loaded = false;
    g_load_in_progress = false;
    g_seekable = false;
    g_core_idle = true;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_stop_mode = true;
    g_hls_mode = false;
    g_last_diag_valid = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_last_diag_log_ms = 0;
    g_load_started_ms = 0;

    g_mpv = mpv_create();
    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        log_error("[player-libmpv] mpv_create failed\n");
        return false;
    }

    mpv_set_option_string(g_mpv, "config", "no");
    mpv_set_option_string(g_mpv, "terminal", "no");
    mpv_set_option_string(g_mpv, "idle", "yes");
    mpv_set_option_string(g_mpv, "input-default-bindings", "no");
    mpv_set_option_string(g_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(g_mpv, "osc", "no");
    mpv_set_option_string(g_mpv, "audio-display", "no");
    mpv_set_option_string(g_mpv, "ao", "null");
    // Step 1 only binds DLNA control to a real playback core. Video rendering
    // stays disabled until the later render/deko3d design is in place.
    mpv_set_option_string(g_mpv, "vo", "null");
    mpv_set_option_string(g_mpv, "network-timeout", PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS);
    mpv_set_option_string(g_mpv, "user-agent", PLAYER_LIBMPV_DEFAULT_USER_AGENT);
    mpv_set_option_string(g_mpv, "hls-bitrate", "max");
    mpv_set_option_string(g_mpv, "demuxer-seekable-cache", "yes");
    mpv_set_option_string(g_mpv, "demuxer-readahead-secs", PLAYER_LIBMPV_DEMUXER_READAHEAD_SECONDS);
    mpv_set_option_string(g_mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_option_string(g_mpv, "demuxer-lavf-probescore", "24");
    // Step 1 focuses on protocol compatibility before CA bundle plumbing exists.
    mpv_set_option_string(g_mpv, "tls-verify", "no");

    int rc = mpv_initialize(g_mpv);
    if (rc < 0)
    {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
        mutexUnlock(&g_state_mutex);
        log_error("[player-libmpv] mpv_initialize failed: %s\n", mpv_error_string(rc));
        return false;
    }

    libmpv_request_log_level_locked(PLAYER_LIBMPV_DEFAULT_LOG_LEVEL);
    mpv_observe_property(g_mpv, LIBMPV_OBS_CORE_IDLE, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_EOF_REACHED, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PLAYBACK_TIME, "playback-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_CACHE_SPEED, "cache-speed", MPV_FORMAT_INT64);
    mpv_observe_property(g_mpv, LIBMPV_OBS_DEMUXER_CACHE_DURATION, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSED_FOR_CACHE, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_DEMUXER_CACHE_STATE, "demuxer-cache-state", MPV_FORMAT_NODE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PLAYBACK_ABORT, "playback-abort", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKING, "seeking", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKABLE, "seekable", MPV_FORMAT_FLAG);

    libmpv_set_double_locked("volume", (double)g_volume);
    libmpv_set_flag_locked("mute", g_mute);
    libmpv_refresh_snapshot_locked();
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);

    log_info("[player-libmpv] init mode=control-only ao=null vo=null net_timeout=%ss tls_verify=no\n",
             PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS);
    return true;
}

static void libmpv_deinit(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (g_mpv)
    {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
    }

    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    g_state = PLAYER_STATE_STOPPED;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_media_loaded = false;
    g_load_in_progress = false;
    g_seekable = false;
    g_core_idle = true;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_stop_mode = true;
    g_hls_mode = false;
    g_last_diag_valid = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_last_diag_log_ms = 0;
    g_load_started_ms = 0;
    g_event_sink = NULL;
    player_source_reset(&g_source);
    g_has_source = false;
    mutexUnlock(&g_state_mutex);

    log_info("[player-libmpv] deinit\n");
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    g_event_sink = sink;
    mutexUnlock(&g_state_mutex);
}

static bool libmpv_set_source(const PlayerResolvedSource *source)
{
    if (!source || source->uri[0] == '\0')
        return false;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    char clipped_uri[160];
    libmpv_clip_for_log(source->uri, clipped_uri, sizeof(clipped_uri));
    g_hls_mode = source->flags.is_hls;
    libmpv_request_log_level_locked(g_hls_mode ? PLAYER_LIBMPV_HLS_LOG_LEVEL : PLAYER_LIBMPV_DEFAULT_LOG_LEVEL);
    libmpv_apply_source_runtime_overrides_locked(source);

    if (!libmpv_load_uri_locked(source->uri, true))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_source = *source;
    g_has_source = true;
    snprintf(g_uri, sizeof(g_uri), "%s", source->uri);
    snprintf(g_metadata, sizeof(g_metadata), "%s", source->metadata);

    g_position_ms = 0;
    g_duration_ms = 0;
    g_stop_mode = true;
    g_media_loaded = false;
    g_load_in_progress = true;
    g_seekable = false;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_load_started_ms = libmpv_now_ms();
    g_last_diag_valid = false;
    g_state = PLAYER_STATE_LOADING;
    libmpv_refresh_snapshot_locked();

    log_info("[player-libmpv] set_source profile=%s uri=%s metadata_len=%zu uri_kind=%s mpv_log=%s\n",
             player_source_profile_name(source->profile),
             clipped_uri,
             strlen(g_metadata),
             g_hls_mode ? "hls" : "default",
             g_hls_mode ? PLAYER_LIBMPV_HLS_LOG_LEVEL : PLAYER_LIBMPV_DEFAULT_LOG_LEVEL);
    libmpv_emit_event_locked(PLAYER_EVENT_URI_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_DURATION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_play(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    libmpv_refresh_snapshot_locked();

    bool reloaded = false;
    if (g_load_in_progress)
    {
        if (!libmpv_set_flag_locked("pause", false))
        {
            mutexUnlock(&g_state_mutex);
            return false;
        }
    }
    else if (!g_media_loaded)
    {
        if (!libmpv_load_uri_locked(g_uri, false))
        {
            mutexUnlock(&g_state_mutex);
            return false;
        }
        g_position_ms = 0;
        g_duration_ms = 0;
        g_media_loaded = false;
        g_load_in_progress = true;
        g_seekable = false;
        reloaded = true;
    }
    else if (!libmpv_set_flag_locked("pause", false))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_stop_mode = false;
    libmpv_refresh_snapshot_locked();

    log_info("[player-libmpv] play reloaded=%d\n", reloaded ? 1 : 0);
    if (reloaded)
        libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_pause(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    libmpv_refresh_snapshot_locked();
    if (g_state != PLAYER_STATE_PLAYING)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_flag_locked("pause", true))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_stop_mode = false;
    libmpv_refresh_snapshot_locked();
    g_state = PLAYER_STATE_PAUSED;

    log_info("[player-libmpv] pause\n");
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_stop(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (g_stop_mode || (!g_media_loaded && !g_load_in_progress))
    {
        g_position_ms = 0;
        g_stop_mode = true;
        g_media_loaded = false;
        g_load_in_progress = false;
        g_seekable = false;
        g_paused_for_cache = false;
        g_seeking = false;
        g_playback_abort = false;
        g_cache_duration_ms = 0;
        g_cache_speed_bps = 0;
        g_state = PLAYER_STATE_STOPPED;
        log_info("[player-libmpv] stop already-stopped=1\n");
        libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
        libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
        mutexUnlock(&g_state_mutex);
        return true;
    }

    if (!libmpv_stop_command_locked())
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_position_ms = 0;
    g_stop_mode = true;
    g_media_loaded = false;
    g_load_in_progress = false;
    g_seekable = false;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_state = PLAYER_STATE_STOPPED;

    log_info("[player-libmpv] stop\n");
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_seek_ms(int position_ms)
{
    if (position_ms < 0)
        return false;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv || g_uri[0] == '\0')
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    libmpv_refresh_snapshot_locked();
    if (!g_media_loaded || !g_seekable)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (g_duration_ms > 0 && position_ms > g_duration_ms)
        position_ms = g_duration_ms;

    if (!libmpv_seek_absolute_locked(position_ms))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_position_ms = position_ms;
    libmpv_refresh_snapshot_locked();
    if (g_stop_mode)
        g_state = PLAYER_STATE_STOPPED;

    log_info("[player-libmpv] seek_ms=%d\n", g_position_ms);
    libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_set_volume(int volume_0_100)
{
    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_double_locked("volume", (double)volume_0_100))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_volume = volume_0_100;
    libmpv_refresh_snapshot_locked();
    log_info("[player-libmpv] set_volume=%d\n", g_volume);
    libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_set_mute(bool mute)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    if (!libmpv_set_flag_locked("mute", mute))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_mute = mute;
    libmpv_refresh_snapshot_locked();
    log_info("[player-libmpv] set_mute=%d\n", g_mute ? 1 : 0);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static int libmpv_get_position_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int position_ms = g_position_ms;
    mutexUnlock(&g_state_mutex);
    return position_ms;
}

static int libmpv_get_duration_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int duration_ms = g_duration_ms;
    mutexUnlock(&g_state_mutex);
    return duration_ms;
}

static int libmpv_get_volume(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    int volume = g_volume;
    mutexUnlock(&g_state_mutex);
    return volume;
}

static bool libmpv_get_mute(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    bool mute = g_mute;
    mutexUnlock(&g_state_mutex);
    return mute;
}

static bool libmpv_is_seekable(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    bool seekable = g_seekable;
    mutexUnlock(&g_state_mutex);
    return seekable;
}

static PlayerState libmpv_get_state(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    libmpv_refresh_snapshot_locked();
    PlayerState state = g_state;
    mutexUnlock(&g_state_mutex);
    return state;
}

#else

static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static bool libmpv_available(void)
{
    return false;
}

static bool libmpv_init(void)
{
    log_warn("[player-libmpv] libmpv backend requested but libmpv is not available in current toolchain\n");
    return false;
}

static void libmpv_deinit(void)
{
    g_event_sink = NULL;
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool libmpv_not_ready(const char *action)
{
    log_warn("[player-libmpv] action=%s rejected backend not built\n", action);
    return false;
}

static bool libmpv_set_source(const PlayerResolvedSource *source)
{
    (void)source;
    return libmpv_not_ready("set_source");
}

static bool libmpv_play(void)
{
    return libmpv_not_ready("play");
}

static bool libmpv_pause(void)
{
    return libmpv_not_ready("pause");
}

static bool libmpv_stop(void)
{
    return libmpv_not_ready("stop");
}

static bool libmpv_seek_ms(int position_ms)
{
    (void)position_ms;
    return libmpv_not_ready("seek_ms");
}

static bool libmpv_set_volume(int volume_0_100)
{
    (void)volume_0_100;
    return libmpv_not_ready("set_volume");
}

static bool libmpv_set_mute(bool mute)
{
    (void)mute;
    return libmpv_not_ready("set_mute");
}

static int libmpv_get_position_ms(void)
{
    return 0;
}

static int libmpv_get_duration_ms(void)
{
    return 0;
}

static int libmpv_get_volume(void)
{
    return 0;
}

static bool libmpv_get_mute(void)
{
    return false;
}

static bool libmpv_is_seekable(void)
{
    return false;
}

static PlayerState libmpv_get_state(void)
{
    return PLAYER_STATE_IDLE;
}

#endif

const PlayerBackendOps g_player_backend_libmpv = {
    .name = "libmpv",
    .available = libmpv_available,
    .init = libmpv_init,
    .deinit = libmpv_deinit,
    .set_event_sink = libmpv_set_event_sink,
    .set_source = libmpv_set_source,
    .play = libmpv_play,
    .pause = libmpv_pause,
    .stop = libmpv_stop,
    .seek_ms = libmpv_seek_ms,
    .set_volume = libmpv_set_volume,
    .set_mute = libmpv_set_mute,
    .get_position_ms = libmpv_get_position_ms,
    .get_duration_ms = libmpv_get_duration_ms,
    .get_volume = libmpv_get_volume,
    .get_mute = libmpv_get_mute,
    .is_seekable = libmpv_is_seekable,
    .get_state = libmpv_get_state,
};
