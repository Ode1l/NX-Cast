#include "player/backend.h"

#include <switch.h>

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

#ifdef HAVE_LIBMPV
#include "player/backend/libmpv_hls.h"
#include "player/ingress/vendor.h"
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#ifdef HAVE_MPV_RENDER_DK3D
#include <mpv/render_dk3d.h>
#endif

#define PLAYER_LIBMPV_URI_MAX 1024
#define PLAYER_LIBMPV_METADATA_MAX 2048
#define PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS "10"
#define PLAYER_LIBMPV_DEMUXER_READAHEAD_SECONDS "8"
#define PLAYER_LIBMPV_HLS_LOG_LEVEL "warn"
#define PLAYER_LIBMPV_DEFAULT_LOG_LEVEL "warn"
#define PLAYER_LIBMPV_HLS_DIAG_INTERVAL_MS 2000
#define PLAYER_LIBMPV_HLS_RUNTIME_DIAG_INTERVAL_MS 8000
#define PLAYER_LIBMPV_HLS_STARTUP_STALL_MS 6000
#define PLAYER_LIBMPV_HLS_STARTUP_STALL_REPEAT_MS 3000
#define PLAYER_LIBMPV_PLAYBACK_STALL_MS 3000
#define PLAYER_LIBMPV_PLAYBACK_STALL_REPEAT_MS 5000
#define PLAYER_LIBMPV_DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"
#define PLAYER_LIBMPV_DEFAULT_AO "hos"
#define PLAYER_LIBMPV_FALLBACK_AO "null"
#ifdef HAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
#define PLAYER_LIBMPV_DEFAULT_HWDEC "nvtegra"
#else
#define PLAYER_LIBMPV_DEFAULT_HWDEC "auto-safe"
#endif
#define PLAYER_LIBMPV_FALLBACK_HWDEC "no"
#define PLAYER_LIBMPV_DEFAULT_HWDEC_CODECS "mpeg1video,mpeg2video,mpeg4,vc1,wmv3,h264,hevc,vp8,vp9,mjpeg"
static void (*g_event_sink)(const PlayerEvent *event) = NULL;

static Mutex g_state_mutex;
static bool g_mutex_ready = false;
static mpv_handle *g_mpv = NULL;
static mpv_render_context *g_render_context = NULL;
static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = PLAYER_DEFAULT_VOLUME;
static bool g_mute = false;
static bool g_media_loaded = false;
static bool g_load_in_progress = false;
static bool g_seekable = false;
static bool g_core_idle = true;
static bool g_paused_for_cache = false;
static bool g_seeking = false;
static bool g_playback_abort = false;
static bool g_stop_mode = true;
static bool g_play_requested = false;
static bool g_hls_mode = false;
static bool g_hls_live_hint = false;
static bool g_has_media = false;
static bool g_last_diag_valid = false;
static PlayerState g_last_diag_state = PLAYER_STATE_IDLE;
static bool g_last_diag_media_loaded = false;
static bool g_last_diag_load_in_progress = false;
static bool g_last_diag_seekable = false;
static bool g_last_diag_core_idle = true;
static bool g_last_diag_paused_for_cache = false;
static bool g_last_diag_seeking = false;
static bool g_last_diag_playback_abort = false;
static LibmpvHlsRuntimeKind g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
static LibmpvHlsRuntimeKind g_last_diag_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
static int g_cache_duration_ms = 0;
static int64_t g_cache_speed_bps = 0;
static uint64_t g_last_diag_log_ms = 0;
static uint64_t g_load_started_ms = 0;
static uint64_t g_file_loaded_ms = 0;
static uint64_t g_first_buffering_ms = 0;
static uint64_t g_first_progress_ms = 0;
static uint64_t g_first_playing_ms = 0;
static uint64_t g_last_hls_stall_log_ms = 0;
static uint64_t g_last_progress_wall_ms = 0;
static int g_last_progress_position_ms = 0;
static bool g_playback_stall_active = false;
static uint64_t g_playback_stall_log_ms = 0;
static bool g_hls_startup_reported = false;
static PlayerMedia g_media;
static char g_uri[PLAYER_LIBMPV_URI_MAX];
static char g_metadata[PLAYER_LIBMPV_METADATA_MAX];
static char g_requested_ao[24];
static char g_requested_hwdec[32];
static volatile bool g_render_update_pending = false;
static const char *g_render_api_name = "none";
static bool g_hwdec_explicit_nvtegra = false;

static void libmpv_clip_for_log(const char *input, char *output, size_t output_size);
static uint64_t libmpv_now_ms(void);
static void libmpv_log_message(const mpv_event_log_message *message);
static const char *libmpv_state_name(PlayerState state);
static int libmpv_ms_from_seconds(double seconds);
static void libmpv_append_option_string(char *options, size_t options_size, const char *extra_options);
static bool libmpv_get_int64_locked(const char *name, int64_t *out);
static bool libmpv_get_string_locked(const char *name, char *out, size_t out_size);
static void libmpv_request_log_level_locked(const char *level);
static bool libmpv_set_option_string_logged_locked(const char *name, const char *value);
static bool libmpv_set_string_property_locked(const char *name, const char *value);
static bool libmpv_set_flag_locked(const char *name, bool value);
static void libmpv_log_backend_runtime_locked(const char *reason);
static void libmpv_apply_media_runtime_overrides_locked(const PlayerMedia *media);
static void libmpv_log_runtime_overrides_detail_locked(const PlayerMedia *media);
static void libmpv_process_event_locked(const mpv_event *event);
static void libmpv_track_hls_startup_locked(PlayerState previous_state);
static void libmpv_sync_properties_locked(bool emit_events);
static void libmpv_log_stream_details_locked(const char *reason);
static void libmpv_maybe_log_diagnostics_locked(const char *reason, bool force);
static void libmpv_maybe_log_playback_stall_locked(uint64_t now_ms);
static void libmpv_render_update(void *ctx);
static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx);
static bool libmpv_render_attach_sw(void);
static void libmpv_render_detach(void);
static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y);
static bool libmpv_render_frame_sw(void *pixels, int width, int height, size_t stride);

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

static void libmpv_render_update(void *ctx)
{
    (void)ctx;
    g_render_update_pending = true;
}

static void libmpv_process_event_locked(const mpv_event *event)
{
    if (!event)
        return;

    switch (event->event_id)
    {
    case MPV_EVENT_START_FILE:
        g_media_loaded = false;
        g_load_in_progress = true;
        g_seekable = false;
        g_load_started_ms = libmpv_now_ms();
        g_file_loaded_ms = 0;
        g_first_buffering_ms = 0;
        g_first_progress_ms = 0;
        g_first_playing_ms = 0;
        g_last_hls_stall_log_ms = 0;
        g_last_progress_wall_ms = 0;
        g_last_progress_position_ms = 0;
        g_playback_stall_active = false;
        g_playback_stall_log_ms = 0;
        g_hls_startup_reported = false;
        g_hls_kind = g_hls_live_hint ? LIBMPV_HLS_RUNTIME_LIVE : LIBMPV_HLS_RUNTIME_UNKNOWN;
        if (g_play_requested)
        {
            g_stop_mode = false;
            if (!libmpv_set_flag_locked("pause", false))
                log_warn("[player-libmpv] failed to reapply play intent on START_FILE\n");
            else
                log_info("[player-libmpv] reapplied play intent on START_FILE\n");
        }
        log_debug("[player-libmpv] event START_FILE\n");
        libmpv_maybe_log_diagnostics_locked("START_FILE", true);
        break;
    case MPV_EVENT_FILE_LOADED:
        g_media_loaded = true;
        g_load_in_progress = false;
        g_file_loaded_ms = libmpv_now_ms();
        log_info("[player-libmpv] event FILE_LOADED load_elapsed_ms=%llu\n",
                 (unsigned long long)(g_load_started_ms == 0 ? 0 : (libmpv_now_ms() - g_load_started_ms)));
        libmpv_log_backend_runtime_locked("FILE_LOADED");
        libmpv_maybe_log_diagnostics_locked("FILE_LOADED", true);
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        log_info("[player-libmpv] event PLAYBACK_RESTART startup_elapsed_ms=%llu\n",
                 (unsigned long long)(g_load_started_ms == 0 ? 0 : (libmpv_now_ms() - g_load_started_ms)));
        libmpv_log_backend_runtime_locked("PLAYBACK_RESTART");
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
        g_file_loaded_ms = 0;
        g_first_buffering_ms = 0;
        g_first_progress_ms = 0;
        g_first_playing_ms = 0;
        g_last_hls_stall_log_ms = 0;
        g_last_progress_wall_ms = 0;
        g_last_progress_position_ms = 0;
        g_playback_stall_active = false;
        g_playback_stall_log_ms = 0;
        g_hls_startup_reported = false;
        g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
        if (end)
        {
            const char *reason = libmpv_end_reason_name(end->reason);
            const char *error = end->error != 0 ? mpv_error_string(end->error) : "none";
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                log_error("[player-libmpv] event END_FILE reason=%s error=%s\n", reason, error);
            else
                log_info("[player-libmpv] event END_FILE reason=%s error=%s\n", reason, error);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
            {
                g_state = PLAYER_STATE_ERROR;
                g_play_requested = false;
            }
            else if (end->reason == MPV_END_FILE_REASON_EOF)
            {
                g_play_requested = false;
            }
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
                libmpv_hls_log_cache_state_node((const mpv_node *)property->data);
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

    if (g_stop_mode &&
        (strstr(message->text, "partial file") != NULL ||
         strstr(message->text, "error reading packet") != NULL ||
         strstr(message->text, "Packet corrupt") != NULL ||
         strstr(message->text, "Invalid NAL unit size") != NULL ||
         strstr(message->text, "missing picture in access unit") != NULL ||
         strstr(message->text, "Error when loading first segment") != NULL ||
         strstr(message->text, "avformat_open_input() failed") != NULL ||
         strstr(message->text, "Leaking 1 nested connections") != NULL))
    {
        char clipped_stop[192];
        libmpv_clip_for_log(message->text, clipped_stop, sizeof(clipped_stop));
        log_debug("[player-libmpv] mpv-stop-noise %s\n", clipped_stop);
        return;
    }

    if (g_media.transport == PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY &&
        strstr(message->text, "mime type is not rfc8216 compliant") != NULL)
    {
        char clipped_local_proxy[192];
        libmpv_clip_for_log(message->text, clipped_local_proxy, sizeof(clipped_local_proxy));
        log_debug("[player-libmpv] mpv-local-proxy-noise %s\n", clipped_local_proxy);
        return;
    }

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

static void libmpv_append_option_string(char *options, size_t options_size, const char *extra_options)
{
    size_t length;

    if (!options || options_size == 0 || !extra_options || extra_options[0] == '\0')
        return;

    length = strlen(options);
    if (length + 1 >= options_size)
        return;

    snprintf(options + length, options_size - length, ",%s", extra_options);
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
        .seekable = g_seekable,
        .error_code = 0,
        .uri = g_uri,
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

static bool libmpv_set_option_string_logged_locked(const char *name, const char *value)
{
    int rc;

    if (!g_mpv || !name || !value)
        return false;

    rc = mpv_set_option_string(g_mpv, name, value);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_option %s=%s failed: %s\n",
                 name,
                 value[0] != '\0' ? value : "<empty>",
                 mpv_error_string(rc));
        return false;
    }

    return true;
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

static void libmpv_log_backend_runtime_locked(const char *reason)
{
    char current_ao[64];
    char current_vo[64];
    char hwdec_current[64];

    current_ao[0] = '\0';
    current_vo[0] = '\0';
    hwdec_current[0] = '\0';

    if (!libmpv_get_string_locked("current-ao", current_ao, sizeof(current_ao)))
        snprintf(current_ao, sizeof(current_ao), "%s", "?");
    if (!libmpv_get_string_locked("current-vo", current_vo, sizeof(current_vo)))
        snprintf(current_vo, sizeof(current_vo), "%s", "?");
    if (!libmpv_get_string_locked("hwdec-current", hwdec_current, sizeof(hwdec_current)))
        snprintf(hwdec_current, sizeof(hwdec_current), "%s", "?");

    log_info("[player-libmpv] backend_runtime reason=%s requested_ao=%s current_ao=%s current_vo=%s requested_hwdec=%s hwdec_current=%s render_path=%s deko3d_header=%d nvtegra_header=%d mpv_nvtegra_backend=%d\n",
             reason ? reason : "refresh",
             g_requested_ao[0] != '\0' ? g_requested_ao : "?",
             current_ao[0] != '\0' ? current_ao : "?",
             current_vo[0] != '\0' ? current_vo : "?",
             g_requested_hwdec[0] != '\0' ? g_requested_hwdec : "?",
             hwdec_current[0] != '\0' ? hwdec_current : "?",
             g_render_api_name ? g_render_api_name : "none",
#ifdef HAVE_MPV_RENDER_DK3D
             1,
#else
             0,
#endif
#ifdef HAVE_NVTEGRA_HWCONTEXT
             1,
#else
             0,
#endif
#ifdef HAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
             1);
#else
             0);
#endif
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
                  g_playback_abort != g_last_diag_playback_abort ||
                  g_hls_kind != g_last_diag_hls_kind;
    }

    if (!changed && g_hls_mode &&
        (g_state == PLAYER_STATE_LOADING || g_state == PLAYER_STATE_BUFFERING || g_state == PLAYER_STATE_SEEKING) &&
        now_ms - g_last_diag_log_ms >= PLAYER_LIBMPV_HLS_DIAG_INTERVAL_MS)
    {
        changed = true;
    }

    if (!changed && g_hls_mode &&
        g_hls_kind == LIBMPV_HLS_RUNTIME_LIVE &&
        now_ms - g_last_diag_log_ms >= PLAYER_LIBMPV_HLS_RUNTIME_DIAG_INTERVAL_MS)
    {
        changed = true;
    }

    if (!changed)
    {
        libmpv_maybe_log_playback_stall_locked(now_ms);
        return;
    }

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

    if (g_state == PLAYER_STATE_ERROR)
    {
        log_error("[player-libmpv] diag reason=%s uri_kind=%s media_format=%s media_hint=%s hls_kind=%s state=%s loaded=%d loading=%d buffering=%d seeking=%d stop_mode=%d core_idle=%d abort=%d seekable=%d via_net=%d pos_ms=%d dur_ms=%d cache_ms=%d cache_speed_bps=%lld file_format=%s demuxer=%s\n",
                  reason ? reason : "refresh",
                  g_hls_mode ? "hls" : "default",
                  ingress_format_name(g_media.format),
                  g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown",
                  libmpv_hls_runtime_kind_name(g_hls_kind),
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
    }
    else
    {
        log_info("[player-libmpv] diag reason=%s uri_kind=%s media_format=%s media_hint=%s hls_kind=%s state=%s loaded=%d loading=%d buffering=%d seeking=%d stop_mode=%d core_idle=%d abort=%d seekable=%d via_net=%d pos_ms=%d dur_ms=%d cache_ms=%d cache_speed_bps=%lld file_format=%s demuxer=%s\n",
                 reason ? reason : "refresh",
                 g_hls_mode ? "hls" : "default",
                 ingress_format_name(g_media.format),
                 g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown",
                 libmpv_hls_runtime_kind_name(g_hls_kind),
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
    }

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
    g_last_diag_hls_kind = g_hls_kind;
    g_last_diag_log_ms = now_ms;
    libmpv_maybe_log_playback_stall_locked(now_ms);
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

static void libmpv_maybe_log_playback_stall_locked(uint64_t now_ms)
{
    if (!g_media_loaded || g_state != PLAYER_STATE_PLAYING)
    {
        if (g_playback_stall_active)
        {
            log_info("[player-libmpv] playback_stall_clear elapsed_ms=%llu pos_ms=%d cache_ms=%d cache_speed_bps=%lld format=%s hint=%s\n",
                     (unsigned long long)(now_ms - g_last_progress_wall_ms),
                     g_position_ms,
                     g_cache_duration_ms,
                     (long long)g_cache_speed_bps,
                     ingress_format_name(g_media.format),
                     g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown");
        }
        g_playback_stall_active = false;
        g_playback_stall_log_ms = 0;
        g_last_progress_wall_ms = now_ms;
        g_last_progress_position_ms = g_position_ms;
        return;
    }

    if (g_position_ms > g_last_progress_position_ms)
    {
        if (g_playback_stall_active)
        {
            log_info("[player-libmpv] playback_stall_clear elapsed_ms=%llu pos_ms=%d cache_ms=%d cache_speed_bps=%lld format=%s hint=%s\n",
                     (unsigned long long)(now_ms - g_last_progress_wall_ms),
                     g_position_ms,
                     g_cache_duration_ms,
                     (long long)g_cache_speed_bps,
                     ingress_format_name(g_media.format),
                     g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown");
        }
        g_playback_stall_active = false;
        g_playback_stall_log_ms = 0;
        g_last_progress_wall_ms = now_ms;
        g_last_progress_position_ms = g_position_ms;
        return;
    }

    if (g_last_progress_wall_ms == 0)
        g_last_progress_wall_ms = now_ms;

    if (!g_playback_stall_active &&
        now_ms - g_last_progress_wall_ms >= PLAYER_LIBMPV_PLAYBACK_STALL_MS)
    {
        g_playback_stall_active = true;
        g_playback_stall_log_ms = now_ms;
        log_warn("[player-libmpv] playback_stall elapsed_ms=%llu pos_ms=%d cache_ms=%d cache_speed_bps=%lld seekable=%d paused_for_cache=%d format=%s hint=%s\n",
                 (unsigned long long)(now_ms - g_last_progress_wall_ms),
                 g_position_ms,
                 g_cache_duration_ms,
                 (long long)g_cache_speed_bps,
                 g_seekable ? 1 : 0,
                 g_paused_for_cache ? 1 : 0,
                 ingress_format_name(g_media.format),
                 g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown");
        libmpv_log_stream_details_locked("PLAYBACK_STALL");
        return;
    }

    if (g_playback_stall_active &&
        now_ms - g_playback_stall_log_ms >= PLAYER_LIBMPV_PLAYBACK_STALL_REPEAT_MS)
    {
        g_playback_stall_log_ms = now_ms;
        log_warn("[player-libmpv] playback_stall elapsed_ms=%llu pos_ms=%d cache_ms=%d cache_speed_bps=%lld seekable=%d paused_for_cache=%d format=%s hint=%s\n",
                 (unsigned long long)(now_ms - g_last_progress_wall_ms),
                 g_position_ms,
                 g_cache_duration_ms,
                 (long long)g_cache_speed_bps,
                 g_seekable ? 1 : 0,
                 g_paused_for_cache ? 1 : 0,
                 ingress_format_name(g_media.format),
                 g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown");
        libmpv_log_stream_details_locked("PLAYBACK_STALL");
    }
}

static void libmpv_apply_media_runtime_overrides_locked(const PlayerMedia *media)
{
    char network_timeout[16];

    if (!media)
        return;

    snprintf(network_timeout,
             sizeof(network_timeout),
             "%d",
             media->network_timeout_seconds > 0 ? media->network_timeout_seconds : 10);
    (void)libmpv_set_string_property_locked("user-agent", media->user_agent);
    (void)libmpv_set_string_property_locked("referrer", media->referrer);
    (void)libmpv_set_string_property_locked("network-timeout", network_timeout);
    (void)libmpv_set_string_property_locked("http-header-fields", media->header_fields);
    (void)libmpv_set_string_property_locked("demuxer-lavf-probe-info", media->probe_info);

    log_info("[player-libmpv] runtime_overrides profile=%s vendor=%s format=%s transport=%s hint=%s hls=%d local_proxy=%d live_hint=%d dash=%d signed=%d bilibili=%d timeout=%s readahead_s=%d probe_info=%s headers=%d load_opts=%d\n",
             ingress_profile_name(media->profile),
             ingress_vendor_name(media->vendor),
             ingress_format_name(media->format),
             ingress_transport_name(media->transport),
             media->format_hint[0] != '\0' ? media->format_hint : "unknown",
             media->format == PLAYER_MEDIA_FORMAT_HLS ? 1 : 0,
             media->transport == PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY ? 1 : 0,
             media->flags.likely_live ? 1 : 0,
             media->format == PLAYER_MEDIA_FORMAT_DASH ? 1 : 0,
             media->flags.is_signed ? 1 : 0,
             media->vendor == PLAYER_MEDIA_VENDOR_BILIBILI ? 1 : 0,
             network_timeout,
             media->demuxer_readahead_seconds,
             media->probe_info[0] != '\0' ? media->probe_info : "auto",
             media->header_fields[0] != '\0' ? 1 : 0,
             media->mpv_load_options[0] != '\0' ? 1 : 0);
    libmpv_log_runtime_overrides_detail_locked(media);
}

static void libmpv_log_runtime_overrides_detail_locked(const PlayerMedia *media)
{
    char clipped_headers[384];
    char clipped_load_opts[320];
    char clipped_user_agent[224];
    char clipped_referrer[224];
    char clipped_origin[160];
    bool has_cookie = false;

    if (!media || !ingress_vendor_is_sensitive(media->vendor))
        return;

    libmpv_clip_for_log(media->header_fields, clipped_headers, sizeof(clipped_headers));
    libmpv_clip_for_log(media->mpv_load_options, clipped_load_opts, sizeof(clipped_load_opts));
    libmpv_clip_for_log(media->user_agent, clipped_user_agent, sizeof(clipped_user_agent));
    libmpv_clip_for_log(media->referrer, clipped_referrer, sizeof(clipped_referrer));
    libmpv_clip_for_log(media->origin, clipped_origin, sizeof(clipped_origin));

    if (strstr(media->header_fields, "Cookie:") || strstr(media->header_fields, "cookie:"))
        has_cookie = true;

    log_info("[player-libmpv] runtime_overrides_detail vendor=%s user_agent=%s referrer=%s origin=%s headers=%s cookie=%d load_opts=%s\n",
             ingress_vendor_name(media->vendor),
             clipped_user_agent[0] != '\0' ? clipped_user_agent : "<empty>",
             clipped_referrer[0] != '\0' ? clipped_referrer : "<empty>",
             clipped_origin[0] != '\0' ? clipped_origin : "<empty>",
             clipped_headers[0] != '\0' ? clipped_headers : "<empty>",
             has_cookie ? 1 : 0,
             clipped_load_opts[0] != '\0' ? clipped_load_opts : "<empty>");
}

static bool libmpv_load_uri_locked(const PlayerMedia *media, const char *uri, bool paused)
{
    char options[512];
    char readahead_option[48];
    const char *cmd[8];
    size_t index = 0;

    snprintf(options, sizeof(options), "pause=%s", paused ? "yes" : "no");
    if (media)
    {
        if (media->demuxer_readahead_seconds > 0)
        {
            snprintf(readahead_option,
                     sizeof(readahead_option),
                     "demuxer-readahead-secs=%d",
                     media->demuxer_readahead_seconds);
            libmpv_append_option_string(options, sizeof(options), readahead_option);
        }
        libmpv_append_option_string(options, sizeof(options), media->mpv_load_options);
    }

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

static void libmpv_track_hls_startup_locked(PlayerState previous_state)
{
    uint64_t now_ms;

    if (!g_hls_mode || g_load_started_ms == 0)
        return;

    now_ms = libmpv_now_ms();

    if (g_paused_for_cache && g_first_buffering_ms == 0)
    {
        g_first_buffering_ms = now_ms;
        log_info("[player-libmpv] hls_startup milestone=buffering elapsed_ms=%llu\n",
                 (unsigned long long)(now_ms - g_load_started_ms));
    }

    if (g_position_ms > 0 && g_first_progress_ms == 0)
    {
        g_first_progress_ms = now_ms;
        log_info("[player-libmpv] hls_startup milestone=first-progress elapsed_ms=%llu file_loaded_after_ms=%llu pos_ms=%d\n",
                 (unsigned long long)(now_ms - g_load_started_ms),
                 (unsigned long long)(g_file_loaded_ms == 0 ? 0 : (g_file_loaded_ms - g_load_started_ms)),
                 g_position_ms);
    }

    if (g_state == PLAYER_STATE_PLAYING && previous_state != PLAYER_STATE_PLAYING && g_first_playing_ms == 0)
    {
        g_first_playing_ms = now_ms;
        log_info("[player-libmpv] hls_startup milestone=playing elapsed_ms=%llu\n",
                 (unsigned long long)(now_ms - g_load_started_ms));
    }

    if (!g_hls_startup_reported && g_first_progress_ms != 0 && g_first_playing_ms != 0)
    {
        log_info("[player-libmpv] hls_startup complete total_ms=%llu file_loaded_after_ms=%llu buffering_after_ms=%llu first_progress_after_ms=%llu kind=%s format=%s hint=%s readahead_s=%d\n",
                 (unsigned long long)(g_first_playing_ms - g_load_started_ms),
                 (unsigned long long)(g_file_loaded_ms == 0 ? 0 : (g_file_loaded_ms - g_load_started_ms)),
                 (unsigned long long)(g_first_buffering_ms == 0 ? 0 : (g_first_buffering_ms - g_load_started_ms)),
                 (unsigned long long)(g_first_progress_ms - g_load_started_ms),
                 libmpv_hls_runtime_kind_name(g_hls_kind),
                 ingress_format_name(g_media.format),
                 g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown",
                 g_media.demuxer_readahead_seconds);
        g_hls_startup_reported = true;
        return;
    }

    if (!g_hls_startup_reported &&
        (g_state == PLAYER_STATE_LOADING || g_state == PLAYER_STATE_BUFFERING || g_state == PLAYER_STATE_SEEKING) &&
        now_ms - g_load_started_ms >= PLAYER_LIBMPV_HLS_STARTUP_STALL_MS &&
        now_ms - g_last_hls_stall_log_ms >= PLAYER_LIBMPV_HLS_STARTUP_STALL_REPEAT_MS)
    {
        g_last_hls_stall_log_ms = now_ms;
        log_warn("[player-libmpv] hls_startup pending elapsed_ms=%llu state=%s kind=%s loaded=%d file_loaded=%d buffering=%d seekable=%d cache_ms=%d readahead_s=%d format=%s hint=%s\n",
                 (unsigned long long)(now_ms - g_load_started_ms),
                 libmpv_state_name(g_state),
                 libmpv_hls_runtime_kind_name(g_hls_kind),
                 g_media_loaded ? 1 : 0,
                 g_file_loaded_ms != 0 ? 1 : 0,
                 g_paused_for_cache ? 1 : 0,
                 g_seekable ? 1 : 0,
                 g_cache_duration_ms,
                 g_media.demuxer_readahead_seconds,
                 ingress_format_name(g_media.format),
                 g_media.format_hint[0] != '\0' ? g_media.format_hint : "unknown");
        libmpv_log_stream_details_locked("HLS_STARTUP_PENDING");
    }
}

static void libmpv_sync_properties_locked(bool emit_events)
{
    PlayerState prev_state = g_state;
    int prev_position_ms = g_position_ms;
    int prev_duration_ms = g_duration_ms;
    int prev_volume = g_volume;
    bool prev_mute = g_mute;
    bool prev_seekable = g_seekable;
    if (!g_mpv)
        return;

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
    char current_demuxer[64];
    char stream_path[192];
    current_demuxer[0] = '\0';
    stream_path[0] = '\0';

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

    if (g_hls_mode)
    {
        if (!libmpv_get_string_locked("current-demuxer", current_demuxer, sizeof(current_demuxer)))
            snprintf(current_demuxer, sizeof(current_demuxer), "%s", "?");
        if (!libmpv_get_string_locked("stream-path", stream_path, sizeof(stream_path)))
            snprintf(stream_path, sizeof(stream_path), "%s", "?");
    }

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
        g_state = PLAYER_STATE_IDLE;
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
    else if (g_media_loaded && !paused && !idle_active)
        g_state = PLAYER_STATE_PLAYING;
    else if (g_stop_mode || (!g_media_loaded && idle_active))
        g_state = PLAYER_STATE_STOPPED;
    else if (paused)
        g_state = PLAYER_STATE_PAUSED;
    else if (idle_active)
        g_state = PLAYER_STATE_STOPPED;
    else
        g_state = PLAYER_STATE_PLAYING;

    if (g_hls_mode)
    {
        LibmpvHlsRuntimeKind runtime_kind = libmpv_hls_detect_runtime_kind(g_hls_live_hint,
                                                                           g_media_loaded,
                                                                           g_seekable,
                                                                           g_duration_ms,
                                                                           stream_path,
                                                                           current_demuxer);
        if (runtime_kind != g_hls_kind)
        {
            g_hls_kind = runtime_kind;
            log_info("[player-libmpv] hls_runtime kind=%s live_hint=%d seekable=%d duration_ms=%d demuxer=%s stream_path=%s\n",
                     libmpv_hls_runtime_kind_name(g_hls_kind),
                     g_hls_live_hint ? 1 : 0,
                     g_seekable ? 1 : 0,
                     g_duration_ms,
                     current_demuxer[0] != '\0' ? current_demuxer : "?",
                     stream_path[0] != '\0' ? stream_path : "?");
        }
    }

    libmpv_track_hls_startup_locked(prev_state);
    libmpv_maybe_log_diagnostics_locked("refresh", false);

    if (!emit_events)
        return;

    if (g_duration_ms != prev_duration_ms)
        libmpv_emit_event_locked(PLAYER_EVENT_DURATION_CHANGED);
    if (g_position_ms != prev_position_ms)
        libmpv_emit_event_locked(PLAYER_EVENT_POSITION_CHANGED);
    if (g_volume != prev_volume)
        libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    if (g_mute != prev_mute)
        libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    if (g_state != prev_state || g_seekable != prev_seekable)
        libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
}

static bool libmpv_pump_events(int timeout_ms)
{
    mpv_event *event;
    bool saw_event = false;
    double timeout_seconds = timeout_ms < 0 ? -1.0 : ((double)timeout_ms / 1000.0);

    if (!g_mpv)
        return false;

    event = mpv_wait_event(g_mpv, timeout_seconds);

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    while (event && event->event_id != MPV_EVENT_NONE)
    {
        saw_event = true;
        libmpv_process_event_locked(event);
        event = mpv_wait_event(g_mpv, 0);
    }

    if (saw_event)
        libmpv_sync_properties_locked(true);
    else
        libmpv_maybe_log_diagnostics_locked("poll", false);

    mutexUnlock(&g_state_mutex);
    return saw_event;
}

static void libmpv_wakeup(void)
{
    if (!g_mpv)
        return;
    mpv_wakeup(g_mpv);
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
    g_state = PLAYER_STATE_IDLE;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = PLAYER_DEFAULT_VOLUME;
    g_mute = false;
    g_media_loaded = false;
    g_load_in_progress = false;
    g_seekable = false;
    g_core_idle = true;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_stop_mode = true;
    g_play_requested = false;
    g_hls_mode = false;
    g_hls_live_hint = false;
    g_has_media = false;
    g_last_diag_valid = false;
    g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
    g_last_diag_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_last_diag_log_ms = 0;
    g_load_started_ms = 0;
    g_file_loaded_ms = 0;
    g_first_buffering_ms = 0;
    g_first_progress_ms = 0;
    g_first_playing_ms = 0;
    g_last_hls_stall_log_ms = 0;
    g_last_progress_wall_ms = 0;
    g_last_progress_position_ms = 0;
    g_playback_stall_active = false;
    g_playback_stall_log_ms = 0;
    g_hls_startup_reported = false;
    g_render_update_pending = false;
    g_hwdec_explicit_nvtegra = false;
    memset(g_requested_ao, 0, sizeof(g_requested_ao));
    memset(g_requested_hwdec, 0, sizeof(g_requested_hwdec));
    ingress_reset(&g_media);

    g_mpv = mpv_create();
    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        log_error("[player-libmpv] mpv_create failed\n");
        return false;
    }

    libmpv_set_option_string_logged_locked("config", "no");
    libmpv_set_option_string_logged_locked("terminal", "no");
    libmpv_set_option_string_logged_locked("idle", "yes");
    libmpv_set_option_string_logged_locked("input-default-bindings", "no");
    libmpv_set_option_string_logged_locked("input-vo-keyboard", "no");
    libmpv_set_option_string_logged_locked("audio-display", "no");
    libmpv_set_option_string_logged_locked("audio-channels", "stereo");
    libmpv_set_option_string_logged_locked("vd-lavc-dr", "yes");
    if (libmpv_set_option_string_logged_locked("ao", PLAYER_LIBMPV_DEFAULT_AO))
        snprintf(g_requested_ao, sizeof(g_requested_ao), "%s", PLAYER_LIBMPV_DEFAULT_AO);
    else
    {
        (void)libmpv_set_option_string_logged_locked("ao", PLAYER_LIBMPV_FALLBACK_AO);
        snprintf(g_requested_ao, sizeof(g_requested_ao), "%s", PLAYER_LIBMPV_FALLBACK_AO);
    }
    libmpv_set_option_string_logged_locked("vo", "libmpv");
#ifdef HAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
    g_hwdec_explicit_nvtegra = true;
#endif
    if (libmpv_set_option_string_logged_locked("hwdec", PLAYER_LIBMPV_DEFAULT_HWDEC))
    {
        snprintf(g_requested_hwdec, sizeof(g_requested_hwdec), "%s", PLAYER_LIBMPV_DEFAULT_HWDEC);
        (void)libmpv_set_option_string_logged_locked("hwdec-codecs", PLAYER_LIBMPV_DEFAULT_HWDEC_CODECS);
    }
    else
    {
        (void)libmpv_set_option_string_logged_locked("hwdec", PLAYER_LIBMPV_FALLBACK_HWDEC);
        snprintf(g_requested_hwdec, sizeof(g_requested_hwdec), "%s", PLAYER_LIBMPV_FALLBACK_HWDEC);
    }
    libmpv_set_option_string_logged_locked("network-timeout", PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS);
    libmpv_set_option_string_logged_locked("user-agent", PLAYER_LIBMPV_DEFAULT_USER_AGENT);
    libmpv_set_option_string_logged_locked("hls-bitrate", "max");
    libmpv_set_option_string_logged_locked("demuxer-seekable-cache", "yes");
    libmpv_set_option_string_logged_locked("demuxer-readahead-secs", PLAYER_LIBMPV_DEMUXER_READAHEAD_SECONDS);
    libmpv_set_option_string_logged_locked("demuxer-lavf-analyzeduration", "0.4");
    libmpv_set_option_string_logged_locked("demuxer-lavf-probescore", "24");
    // Step 1 focuses on protocol compatibility before CA bundle plumbing exists.
    libmpv_set_option_string_logged_locked("tls-verify", "no");

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
    libmpv_sync_properties_locked(false);
    libmpv_log_backend_runtime_locked("init");
    libmpv_emit_event_locked(PLAYER_EVENT_STATE_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_VOLUME_CHANGED);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);

    if (!g_hwdec_explicit_nvtegra)
    {
        log_info("[player-libmpv] toolchain lacks explicit nvtegra hwdec backend in libmpv; using hwdec=%s\n",
                 g_requested_hwdec[0] != '\0' ? g_requested_hwdec : PLAYER_LIBMPV_FALLBACK_HWDEC);
    }

    log_info("[player-libmpv] init mode=render-api ao=%s vo=libmpv hwdec=%s net_timeout=%ss tls_verify=no render_path=%s deko3d_header=%d nvtegra_header=%d mpv_nvtegra_backend=%d\n",
             g_requested_ao[0] != '\0' ? g_requested_ao : PLAYER_LIBMPV_FALLBACK_AO,
             g_requested_hwdec[0] != '\0' ? g_requested_hwdec : PLAYER_LIBMPV_FALLBACK_HWDEC,
             PLAYER_LIBMPV_NETWORK_TIMEOUT_SECONDS,
             g_render_api_name ? g_render_api_name : "none",
#ifdef HAVE_MPV_RENDER_DK3D
             1,
#else
             0,
#endif
#ifdef HAVE_NVTEGRA_HWCONTEXT
             1,
#else
             0,
#endif
#ifdef HAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
             1);
#else
             0);
#endif
    return true;
}

static void libmpv_deinit(void)
{
    libmpv_render_detach();
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (g_mpv)
    {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
    }

    memset(g_uri, 0, sizeof(g_uri));
    memset(g_metadata, 0, sizeof(g_metadata));
    g_state = PLAYER_STATE_IDLE;
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
    g_play_requested = false;
    g_hls_mode = false;
    g_hls_live_hint = false;
    g_last_diag_valid = false;
    g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
    g_last_diag_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_last_diag_log_ms = 0;
    g_load_started_ms = 0;
    g_file_loaded_ms = 0;
    g_first_buffering_ms = 0;
    g_first_progress_ms = 0;
    g_first_playing_ms = 0;
    g_last_hls_stall_log_ms = 0;
    g_hls_startup_reported = false;
    g_render_update_pending = false;
    g_event_sink = NULL;
    memset(g_requested_ao, 0, sizeof(g_requested_ao));
    memset(g_requested_hwdec, 0, sizeof(g_requested_hwdec));
    ingress_reset(&g_media);
    g_has_media = false;
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

static bool libmpv_set_media(const PlayerMedia *media)
{
    if (!media || media->uri[0] == '\0')
        return false;

    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);

    if (!g_mpv)
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    char clipped_uri[160];
    libmpv_clip_for_log(media->uri, clipped_uri, sizeof(clipped_uri));
    g_hls_mode = media->format == PLAYER_MEDIA_FORMAT_HLS;
    g_hls_live_hint = media->flags.likely_live;
    g_hls_kind = g_hls_mode && g_hls_live_hint ? LIBMPV_HLS_RUNTIME_LIVE : LIBMPV_HLS_RUNTIME_UNKNOWN;
    libmpv_request_log_level_locked(g_hls_mode ? PLAYER_LIBMPV_HLS_LOG_LEVEL : PLAYER_LIBMPV_DEFAULT_LOG_LEVEL);
    libmpv_apply_media_runtime_overrides_locked(media);

    if (!libmpv_load_uri_locked(media, media->uri, true))
    {
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_media = *media;
    g_has_media = true;
    snprintf(g_uri, sizeof(g_uri), "%s", media->uri);
    snprintf(g_metadata, sizeof(g_metadata), "%s", media->metadata);

    g_position_ms = 0;
    g_duration_ms = 0;
    g_stop_mode = true;
    g_play_requested = false;
    g_media_loaded = false;
    g_load_in_progress = true;
    g_seekable = false;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_load_started_ms = libmpv_now_ms();
    g_file_loaded_ms = 0;
    g_first_buffering_ms = 0;
    g_first_progress_ms = 0;
    g_first_playing_ms = 0;
    g_last_hls_stall_log_ms = 0;
    g_hls_startup_reported = false;
    g_last_diag_valid = false;
    g_last_diag_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
    g_state = PLAYER_STATE_LOADING;
    libmpv_sync_properties_locked(false);

    log_info("[player-libmpv] set_media profile=%s vendor=%s format=%s transport=%s hint=%s uri=%s metadata_len=%zu uri_kind=%s live_hint=%d readahead_s=%d mpv_log_threshold=%s\n",
             ingress_profile_name(media->profile),
             ingress_vendor_name(media->vendor),
             ingress_format_name(media->format),
             ingress_transport_name(media->transport),
             media->format_hint[0] != '\0' ? media->format_hint : "unknown",
             clipped_uri,
             strlen(g_metadata),
             g_hls_mode ? (g_hls_live_hint ? "hls-live-hint" : "hls") : "default",
             g_hls_live_hint ? 1 : 0,
             media->demuxer_readahead_seconds,
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

    g_play_requested = true;
    libmpv_sync_properties_locked(false);

    bool reloaded = false;
    if (g_load_in_progress)
    {
        if (!libmpv_set_flag_locked("pause", false))
        {
            g_play_requested = false;
            mutexUnlock(&g_state_mutex);
            return false;
        }
    }
    else if (!g_media_loaded)
    {
        if (!libmpv_load_uri_locked(g_has_media ? &g_media : NULL, g_uri, false))
        {
            g_play_requested = false;
            mutexUnlock(&g_state_mutex);
            return false;
        }
        g_position_ms = 0;
        g_duration_ms = 0;
        g_media_loaded = false;
        g_load_in_progress = true;
        g_seekable = false;
        g_load_started_ms = libmpv_now_ms();
        g_file_loaded_ms = 0;
        g_first_buffering_ms = 0;
        g_first_progress_ms = 0;
        g_first_playing_ms = 0;
        g_last_hls_stall_log_ms = 0;
        g_hls_startup_reported = false;
        g_hls_kind = g_hls_mode && g_hls_live_hint ? LIBMPV_HLS_RUNTIME_LIVE : LIBMPV_HLS_RUNTIME_UNKNOWN;
        reloaded = true;
    }
    else if (!libmpv_set_flag_locked("pause", false))
    {
        g_play_requested = false;
        mutexUnlock(&g_state_mutex);
        return false;
    }

    g_stop_mode = false;
    libmpv_sync_properties_locked(false);

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

    libmpv_sync_properties_locked(false);
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

    g_play_requested = false;
    g_stop_mode = false;
    libmpv_sync_properties_locked(false);
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
        g_play_requested = false;
        g_media_loaded = false;
        g_load_in_progress = false;
        g_seekable = false;
        g_paused_for_cache = false;
        g_seeking = false;
        g_playback_abort = false;
        g_cache_duration_ms = 0;
        g_cache_speed_bps = 0;
        g_file_loaded_ms = 0;
        g_first_buffering_ms = 0;
        g_first_progress_ms = 0;
        g_first_playing_ms = 0;
        g_last_hls_stall_log_ms = 0;
        g_hls_startup_reported = false;
        g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
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
    g_play_requested = false;
    g_media_loaded = false;
    g_load_in_progress = false;
    g_seekable = false;
    g_paused_for_cache = false;
    g_seeking = false;
    g_playback_abort = false;
    g_cache_duration_ms = 0;
    g_cache_speed_bps = 0;
    g_file_loaded_ms = 0;
    g_first_buffering_ms = 0;
    g_first_progress_ms = 0;
    g_first_playing_ms = 0;
    g_last_hls_stall_log_ms = 0;
    g_last_progress_wall_ms = 0;
    g_last_progress_position_ms = 0;
    g_playback_stall_active = false;
    g_playback_stall_log_ms = 0;
    g_hls_startup_reported = false;
    g_hls_kind = LIBMPV_HLS_RUNTIME_UNKNOWN;
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

    libmpv_sync_properties_locked(false);
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
    libmpv_sync_properties_locked(false);
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
    libmpv_sync_properties_locked(false);
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
    libmpv_sync_properties_locked(false);
    log_info("[player-libmpv] set_mute=%d\n", g_mute ? 1 : 0);
    libmpv_emit_event_locked(PLAYER_EVENT_MUTE_CHANGED);
    mutexUnlock(&g_state_mutex);
    return true;
}

static bool libmpv_render_supported(void)
{
    return true;
}

static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    if (!g_mpv || !get_proc_address)
        return false;

    if (g_render_context)
    {
        if (g_render_api_name && strcmp(g_render_api_name, "gl") == 0)
            return true;
        libmpv_render_detach();
    }

    mpv_opengl_init_params gl_init_params = {
        .get_proc_address = get_proc_address,
        .get_proc_address_ctx = get_proc_address_ctx,
    };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, NULL},
    };

    int rc = mpv_render_context_create(&g_render_context, g_mpv, params);
    if (rc < 0)
    {
        g_render_context = NULL;
        g_render_api_name = "none";
        log_error("[player-libmpv] render_attach_gl failed: %s\n", mpv_error_string(rc));
        return false;
    }

    g_render_update_pending = true;
    g_render_api_name = "gl";
    mpv_render_context_set_update_callback(g_render_context, libmpv_render_update, NULL);
    log_info("[player-libmpv] render_attach api=gl\n");
    return true;
}

static bool libmpv_render_attach_sw(void)
{
    if (!g_mpv)
        return false;

    if (g_render_context)
    {
        if (g_render_api_name && strcmp(g_render_api_name, "sw") == 0)
            return true;
        libmpv_render_detach();
    }

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, NULL},
    };

    int rc = mpv_render_context_create(&g_render_context, g_mpv, params);
    if (rc < 0)
    {
        g_render_context = NULL;
        log_error("[player-libmpv] render_attach_sw failed: %s\n", mpv_error_string(rc));
        return false;
    }

    g_render_update_pending = true;
    g_render_api_name = "sw";
    mpv_render_context_set_update_callback(g_render_context, libmpv_render_update, NULL);
    log_info("[player-libmpv] render_attach api=sw\n");
    return true;
}

static void libmpv_render_detach(void)
{
    if (!g_render_context)
        return;

    mpv_render_context_set_update_callback(g_render_context, NULL, NULL);
    mpv_render_context_free(g_render_context);
    g_render_context = NULL;
    g_render_update_pending = false;
    log_info("[player-libmpv] render_detach api=%s\n", g_render_api_name ? g_render_api_name : "none");
    g_render_api_name = "none";
}

static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    if (!g_render_context || width <= 0 || height <= 0)
        return false;

    if (g_render_update_pending)
    {
        (void)mpv_render_context_update(g_render_context);
        g_render_update_pending = false;
    }

    mpv_opengl_fbo gl_fbo = {
        .fbo = fbo,
        .w = width,
        .h = height,
        .internal_format = 0,
    };
    int flip = flip_y ? 1 : 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &gl_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, NULL},
    };

    int rc = mpv_render_context_render(g_render_context, params);
    if (rc < 0)
    {
        log_warn("[player-libmpv] render_frame_gl failed: %s\n", mpv_error_string(rc));
        return false;
    }

    return true;
}

static bool libmpv_render_frame_sw(void *pixels, int width, int height, size_t stride)
{
    if (!g_render_context || !pixels || width <= 0 || height <= 0 || stride == 0)
        return false;

    if (g_render_update_pending)
    {
        (void)mpv_render_context_update(g_render_context);
        g_render_update_pending = false;
    }

    int size[2] = {width, height};
    size_t render_stride = stride;
    char format[] = "rgb0";
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SW_SIZE, size},
        {MPV_RENDER_PARAM_SW_FORMAT, format},
        {MPV_RENDER_PARAM_SW_STRIDE, &render_stride},
        {MPV_RENDER_PARAM_SW_POINTER, pixels},
        {MPV_RENDER_PARAM_INVALID, NULL},
    };

    int rc = mpv_render_context_render(g_render_context, params);
    if (rc < 0)
    {
        log_warn("[player-libmpv] render_frame_sw failed: %s\n", mpv_error_string(rc));
        return false;
    }

    return true;
}

static int libmpv_get_position_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    int position_ms = g_position_ms;
    mutexUnlock(&g_state_mutex);
    return position_ms;
}

static int libmpv_get_duration_ms(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    int duration_ms = g_duration_ms;
    mutexUnlock(&g_state_mutex);
    return duration_ms;
}

static int libmpv_get_volume(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    int volume = g_volume;
    mutexUnlock(&g_state_mutex);
    return volume;
}

static bool libmpv_get_mute(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    bool mute = g_mute;
    mutexUnlock(&g_state_mutex);
    return mute;
}

static bool libmpv_is_seekable(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
    bool seekable = g_seekable;
    mutexUnlock(&g_state_mutex);
    return seekable;
}

static PlayerState libmpv_get_state(void)
{
    libmpv_ensure_mutex();
    mutexLock(&g_state_mutex);
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

static bool libmpv_set_media(const PlayerMedia *media)
{
    (void)media;
    return libmpv_not_ready("set_media");
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

static bool libmpv_pump_events(int timeout_ms)
{
    (void)timeout_ms;
    return false;
}

static void libmpv_wakeup(void)
{
}

static bool libmpv_render_supported(void)
{
    return false;
}

static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    (void)get_proc_address;
    (void)get_proc_address_ctx;
    return libmpv_not_ready("render_attach_gl");
}

static bool libmpv_render_attach_sw(void)
{
    return libmpv_not_ready("render_attach_sw");
}

static void libmpv_render_detach(void)
{
}

static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    (void)fbo;
    (void)width;
    (void)height;
    (void)flip_y;
    return false;
}

static bool libmpv_render_frame_sw(void *pixels, int width, int height, size_t stride)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    return false;
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

const BackendOps g_libmpv_ops = {
    .name = "libmpv",
    .available = libmpv_available,
    .init = libmpv_init,
    .deinit = libmpv_deinit,
    .set_event_sink = libmpv_set_event_sink,
    .set_media = libmpv_set_media,
    .play = libmpv_play,
    .pause = libmpv_pause,
    .stop = libmpv_stop,
    .seek_ms = libmpv_seek_ms,
    .set_volume = libmpv_set_volume,
    .set_mute = libmpv_set_mute,
    .pump_events = libmpv_pump_events,
    .wakeup = libmpv_wakeup,
    .render_supported = libmpv_render_supported,
    .render_attach_gl = libmpv_render_attach_gl,
    .render_attach_sw = libmpv_render_attach_sw,
    .render_detach = libmpv_render_detach,
    .render_frame_gl = libmpv_render_frame_gl,
    .render_frame_sw = libmpv_render_frame_sw,
    .get_position_ms = libmpv_get_position_ms,
    .get_duration_ms = libmpv_get_duration_ms,
    .get_volume = libmpv_get_volume,
    .get_mute = libmpv_get_mute,
    .is_seekable = libmpv_is_seekable,
    .get_state = libmpv_get_state,
};
