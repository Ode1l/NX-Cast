#include "player/backend.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <switch.h>

#include "app/runtime_observability.h"
#include "log/log.h"
#include "player/backend/libmpv_airplay.h"
#include "player/core/ownership.h"
#include "player/seek_target.h"
#include "player/trace.h"
#include "protocol/airplay/media/stream_bridge.h"

#ifdef HAVE_LIBMPV
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/stream_cb.h>
#ifdef HAVE_MPV_RENDER_GL
#include <mpv/render_gl.h>
#endif
#ifdef HAVE_MPV_RENDER_DK3D
#include <mpv/render_dk3d.h>
#endif
#endif

#ifdef HAVE_LIBMPV

typedef enum
{
    LIBMPV_OBS_VOLUME = 1,
    LIBMPV_OBS_TIME_POS,
    LIBMPV_OBS_PAUSE,
    LIBMPV_OBS_MUTE,
    LIBMPV_OBS_DURATION,
    LIBMPV_OBS_SEEKABLE,
    LIBMPV_OBS_PAUSED_FOR_CACHE,
    LIBMPV_OBS_SEEKING
} LibmpvObservedProperty;

typedef enum
{
    LIBMPV_REPLY_LOADFILE = 100,
    LIBMPV_REPLY_PLAY,
    LIBMPV_REPLY_PAUSE,
    LIBMPV_REPLY_STOP,
    LIBMPV_REPLY_SEEK,
    LIBMPV_REPLY_SET_VOLUME,
    LIBMPV_REPLY_SET_MUTE
} LibmpvReplyId;

typedef struct
{
    PlayerEvent events[4];
    size_t count;
} LibmpvPendingEvents;

typedef enum
{
    LIBMPV_LOG_NOISE_NONE = -1,
    LIBMPV_LOG_NOISE_HTTP_PREMATURE = 0,
    LIBMPV_LOG_NOISE_H264,
    LIBMPV_LOG_NOISE_AAC,
    LIBMPV_LOG_NOISE_DECODE,
    LIBMPV_LOG_NOISE_COUNT
} LibmpvLogNoise;

#define LIBMPV_LOG_NOISE_KEEP 8u
#define LIBMPV_TRACE_URL_MAX 160
#define LIBMPV_INITIAL_SEEK_WINDOW_MS 350ULL
#define LIBMPV_INITIAL_SEEK_TIMEOUT_MS 3000ULL
#define LIBMPV_STARTUP_GATE_TIMEOUT_MS 15000ULL

static void (*g_event_sink)(const PlayerEvent *event) = NULL;
static Mutex g_mutex;
static bool g_sync_ready = false;

static mpv_handle *g_mpv = NULL;
static mpv_render_context *g_render_ctx = NULL;
static AirPlayStreamBridge *g_airplay_stream_bridge = NULL;
static bool g_airplay_stream_registered = false;
static bool g_render_update_pending = false;

typedef enum
{
    LIBMPV_RENDER_BACKEND_NONE = 0,
    LIBMPV_RENDER_BACKEND_GL,
    LIBMPV_RENDER_BACKEND_DK3D
} LibmpvRenderBackend;

static LibmpvRenderBackend g_render_backend = LIBMPV_RENDER_BACKEND_NONE;

static bool g_has_media = false;
static char *g_uri = NULL;
static PlayerState g_state = PLAYER_STATE_IDLE;
static int g_position_ms = 0;
static int g_duration_ms = 0;
static int g_volume = PLAYER_DEFAULT_VOLUME;
static bool g_mute = false;
static bool g_seekable = false;
static bool g_pause = true;
static bool g_paused_for_cache = false;
static bool g_seeking = false;
static bool g_pending_seek = false;
static char *g_pending_seek_target = NULL;
static bool g_stopped = false;
static bool g_file_loaded = false;
static bool g_load_pending = false;
static bool g_startup_render_hold = false;
static bool g_startup_restart_seen = false;
static bool g_initial_seek_inflight = false;
static bool g_initial_seek_event_seen = false;
static bool g_play_requested = false;
static bool g_play_unpause_pending = false;
static uint64_t g_startup_hold_deadline_ms = 0;
static int g_last_error = 0;
static bool g_process_volume_active = false;
static u64 g_process_volume_pid = 0;
static unsigned int g_log_noise_seen[LIBMPV_LOG_NOISE_COUNT];
static unsigned int g_log_noise_suppressed[LIBMPV_LOG_NOISE_COUNT];

#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
static bool g_dlna_observe_active = false;
static uint64_t g_dlna_observe_last_ms = 0u;
static int g_dlna_observe_http_status = -1;
static char g_dlna_observe_range_stage[24] = "unknown";
#endif

static void libmpv_on_render_update(void *ctx);

static uint64_t libmpv_monotonic_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static void libmpv_log_resource_boundary(const char *event)
{
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    PlayerOwnershipLease lease = {0};

    if (player_ownership_current(&lease))
    {
        runtime_observability_log_resources(
            event, player_media_owner_name(lease.owner), lease.generation);
    }
    else
    {
        runtime_observability_log_resources(event, "none", 0u);
    }
#else
    (void)event;
#endif
}

#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY

typedef struct
{
    bool cache_duration_available;
    double cache_duration;
    bool forward_bytes_available;
    int64_t forward_bytes;
    bool total_bytes_available;
    int64_t total_bytes;
    bool reader_pts_available;
    double reader_pts;
    bool cache_end_available;
    double cache_end;
    bool eof_available;
    int eof;
    bool underrun_available;
    int underrun;
} LibmpvDlnaCacheObservation;

static const char *libmpv_observe_state_name(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_LOADING:
        return "loading";
    case PLAYER_STATE_BUFFERING:
        return "buffering";
    case PLAYER_STATE_SEEKING:
        return "seeking";
    case PLAYER_STATE_PLAYING:
        return "playing";
    case PLAYER_STATE_PAUSED:
        return "paused";
    case PLAYER_STATE_STOPPED:
        return "stopped";
    case PLAYER_STATE_ERROR:
        return "error";
    case PLAYER_STATE_IDLE:
    default:
        return "idle";
    }
}

static const mpv_node *libmpv_observe_map_value(const mpv_node *node,
                                                 const char *key)
{
    mpv_node_list *map;

    if (!node || node->format != MPV_FORMAT_NODE_MAP || !node->u.list ||
        !key)
        return NULL;
    map = node->u.list;
    for (int index = 0; index < map->num; ++index)
    {
        if (map->keys && map->keys[index] &&
            strcmp(map->keys[index], key) == 0)
            return &map->values[index];
    }
    return NULL;
}

static bool libmpv_observe_node_int64(const mpv_node *node, int64_t *out)
{
    if (!node || !out)
        return false;
    if (node->format == MPV_FORMAT_INT64)
    {
        *out = node->u.int64;
        return true;
    }
    if (node->format == MPV_FORMAT_FLAG)
    {
        *out = node->u.flag;
        return true;
    }
    return false;
}

static bool libmpv_observe_node_double(const mpv_node *node, double *out)
{
    if (!node || !out)
        return false;
    if (node->format == MPV_FORMAT_DOUBLE)
    {
        *out = node->u.double_;
        return true;
    }
    if (node->format == MPV_FORMAT_INT64)
    {
        *out = (double)node->u.int64;
        return true;
    }
    return false;
}

static void libmpv_observe_cache(mpv_handle *mpv,
                                 LibmpvDlnaCacheObservation *out)
{
    mpv_node cache = {0};
    const mpv_node *value;
    int64_t integer;

    if (!mpv || !out)
        return;
    memset(out, 0, sizeof(*out));
    if (mpv_get_property(mpv, "demuxer-cache-state", MPV_FORMAT_NODE,
                         &cache) < 0)
    {
        out->cache_duration_available =
            mpv_get_property(mpv, "demuxer-cache-duration",
                             MPV_FORMAT_DOUBLE, &out->cache_duration) >= 0;
        return;
    }

    value = libmpv_observe_map_value(&cache, "cache-duration");
    out->cache_duration_available =
        libmpv_observe_node_double(value, &out->cache_duration);
    value = libmpv_observe_map_value(&cache, "fw-bytes");
    out->forward_bytes_available =
        libmpv_observe_node_int64(value, &out->forward_bytes);
    value = libmpv_observe_map_value(&cache, "total-bytes");
    out->total_bytes_available =
        libmpv_observe_node_int64(value, &out->total_bytes);
    value = libmpv_observe_map_value(&cache, "reader-pts");
    out->reader_pts_available =
        libmpv_observe_node_double(value, &out->reader_pts);
    value = libmpv_observe_map_value(&cache, "cache-end");
    out->cache_end_available =
        libmpv_observe_node_double(value, &out->cache_end);
    value = libmpv_observe_map_value(&cache, "eof");
    if (libmpv_observe_node_int64(value, &integer))
    {
        out->eof_available = true;
        out->eof = integer != 0;
    }
    value = libmpv_observe_map_value(&cache, "underrun");
    if (libmpv_observe_node_int64(value, &integer))
    {
        out->underrun_available = true;
        out->underrun = integer != 0;
    }
    mpv_free_node_contents(&cache);
}

static const char *libmpv_observe_find_ascii(const char *text,
                                              const char *needle)
{
    size_t needle_length;

    if (!text || !needle || !needle[0])
        return NULL;
    needle_length = strlen(needle);
    for (; *text; ++text)
    {
        size_t index = 0u;

        while (index < needle_length && text[index] &&
               ((text[index] >= 'A' && text[index] <= 'Z'
                     ? text[index] - 'A' + 'a'
                     : text[index]) ==
                (needle[index] >= 'A' && needle[index] <= 'Z'
                     ? needle[index] - 'A' + 'a'
                     : needle[index])))
        {
            ++index;
        }
        if (index == needle_length)
            return text;
    }
    return NULL;
}

static bool libmpv_observe_contains_ascii(const char *text,
                                          const char *needle)
{
    return libmpv_observe_find_ascii(text, needle) != NULL;
}

static int libmpv_observe_http_status(const char *text)
{
    const char *cursor = libmpv_observe_find_ascii(text, "http/");

    if (!cursor)
        cursor = libmpv_observe_find_ascii(text, "http status");
    if (!cursor)
        cursor = libmpv_observe_find_ascii(text, "http error");
    if (!cursor)
        return -1;
    for (; *cursor; ++cursor)
    {
        if (cursor[0] >= '1' && cursor[0] <= '5' &&
            cursor[1] >= '0' && cursor[1] <= '9' &&
            cursor[2] >= '0' && cursor[2] <= '9')
        {
            return (cursor[0] - '0') * 100 + (cursor[1] - '0') * 10 +
                   (cursor[2] - '0');
        }
    }
    return -1;
}

static void libmpv_observe_mpv_log(const mpv_event_log_message *message)
{
    int status;
    bool range;

    if (!message || !message->text)
        return;
    status = libmpv_observe_http_status(message->text);
    range = libmpv_observe_contains_ascii(message->text, "range");
    if (status < 0 && !range)
        return;

    mutexLock(&g_mutex);
    if (g_dlna_observe_active)
    {
        if (status >= 0)
            g_dlna_observe_http_status = status;
        if (range)
            snprintf(g_dlna_observe_range_stage,
                     sizeof(g_dlna_observe_range_stage), "range-log");
    }
    mutexUnlock(&g_mutex);
}

static void libmpv_observe_dlna_sample(void)
{
    LibmpvDlnaCacheObservation cache;
    mpv_handle *mpv;
    PlayerState state;
    uint64_t now_ms = libmpv_monotonic_ms();
    int http_status;
    char range_stage[sizeof(g_dlna_observe_range_stage)];
    char *video_format = NULL;
    char *hwdec = NULL;
    int64_t video_track = -1;
    int64_t presented = -1;
    int64_t vo_dropped = -1;
    int64_t decoder_dropped = -1;
    char forward_bytes[32];
    char total_bytes[32];
    char cache_duration[32];
    char reader_pts[32];
    char cache_end[32];
    char eof[16];
    char underrun[16];
    char http[16];
    char video_track_text[32];
    char presented_text[32];
    char vo_dropped_text[32];
    char decoder_dropped_text[32];

    mutexLock(&g_mutex);
    state = g_state;
    if (!g_mpv || !g_dlna_observe_active ||
        (state != PLAYER_STATE_LOADING && state != PLAYER_STATE_BUFFERING &&
         state != PLAYER_STATE_SEEKING) ||
        (g_dlna_observe_last_ms > 0u &&
         now_ms - g_dlna_observe_last_ms < 1000u))
    {
        mutexUnlock(&g_mutex);
        return;
    }
    g_dlna_observe_last_ms = now_ms;
    mpv = g_mpv;
    http_status = g_dlna_observe_http_status;
    snprintf(range_stage, sizeof(range_stage), "%s",
             g_dlna_observe_range_stage);
    mutexUnlock(&g_mutex);

    libmpv_observe_cache(mpv, &cache);
    video_format = mpv_get_property_string(mpv, "video-format");
    hwdec = mpv_get_property_string(mpv, "hwdec-current");
    (void)mpv_get_property(mpv, "vid", MPV_FORMAT_INT64, &video_track);
    (void)mpv_get_property(mpv, "estimated-frame-number",
                           MPV_FORMAT_INT64, &presented);
    (void)mpv_get_property(mpv, "frame-drop-count", MPV_FORMAT_INT64,
                           &vo_dropped);
    (void)mpv_get_property(mpv, "decoder-frame-drop-count",
                           MPV_FORMAT_INT64, &decoder_dropped);

#define LIBMPV_DIAG_FORMAT_VALUE(buffer, available, format, value)            \
    snprintf((buffer), sizeof(buffer), (available) ? (format) : "unknown",   \
             (value))
    LIBMPV_DIAG_FORMAT_VALUE(forward_bytes, cache.forward_bytes_available,
                             "%lld", (long long)cache.forward_bytes);
    LIBMPV_DIAG_FORMAT_VALUE(total_bytes, cache.total_bytes_available, "%lld",
                             (long long)cache.total_bytes);
    LIBMPV_DIAG_FORMAT_VALUE(cache_duration,
                             cache.cache_duration_available, "%.3f",
                             cache.cache_duration);
    LIBMPV_DIAG_FORMAT_VALUE(reader_pts, cache.reader_pts_available, "%.3f",
                             cache.reader_pts);
    LIBMPV_DIAG_FORMAT_VALUE(cache_end, cache.cache_end_available, "%.3f",
                             cache.cache_end);
    LIBMPV_DIAG_FORMAT_VALUE(eof, cache.eof_available, "%d", cache.eof);
    LIBMPV_DIAG_FORMAT_VALUE(underrun, cache.underrun_available, "%d",
                             cache.underrun);
    LIBMPV_DIAG_FORMAT_VALUE(http, http_status >= 0, "%d", http_status);
    LIBMPV_DIAG_FORMAT_VALUE(video_track_text, video_track >= 0, "%lld",
                             (long long)video_track);
    LIBMPV_DIAG_FORMAT_VALUE(presented_text, presented >= 0, "%lld",
                             (long long)presented);
    LIBMPV_DIAG_FORMAT_VALUE(vo_dropped_text, vo_dropped >= 0, "%lld",
                             (long long)vo_dropped);
    LIBMPV_DIAG_FORMAT_VALUE(decoder_dropped_text, decoder_dropped >= 0,
                             "%lld", (long long)decoder_dropped);
#undef LIBMPV_DIAG_FORMAT_VALUE

    log_info(
        "[dlna-player-diag] phase=%s cache_bytes=%s/%s cache_s=%s "
        "reader_pts=%s cache_end=%s eof=%s underrun=%s http=%s "
        "range=%s video=%s/%s hwdec=%s "
        "frames=decoded:unknown/presented:%s/dropped:%s+%s\n",
        libmpv_observe_state_name(state), forward_bytes, total_bytes,
        cache_duration, reader_pts, cache_end, eof, underrun, http,
        range_stage, video_track_text,
        video_format ? video_format : "unknown",
        hwdec ? hwdec : "unknown", presented_text, vo_dropped_text,
        decoder_dropped_text);

    mpv_free(video_format);
    mpv_free(hwdec);
}

#else

static void libmpv_observe_mpv_log(const mpv_event_log_message *message)
{
    (void)message;
}

static void libmpv_observe_dlna_sample(void)
{
}

#endif

static bool libmpv_is_direct_mp4(const char *uri)
{
    const char *end;

    if (!uri || !uri[0])
        return false;
    end = strpbrk(uri, "?#");
    if (!end)
        end = uri + strlen(uri);
    return end - uri >= 4 && strncasecmp(end - 4, ".mp4", 4) == 0;
}

static void libmpv_ensure_sync(void)
{
    if (g_sync_ready)
        return;
    mutexInit(&g_mutex);
    g_sync_ready = true;
}

static int64_t libmpv_airplay_stream_read(void *cookie, char *buffer,
                                          uint64_t size)
{
    size_t request = size > SIZE_MAX ? SIZE_MAX : (size_t)size;

    return airplay_stream_bridge_read(cookie, (uint8_t *)buffer, request);
}

static void libmpv_airplay_stream_cancel(void *cookie)
{
    airplay_stream_bridge_cancel(cookie);
}

static void libmpv_airplay_stream_close(void *cookie)
{
    AirPlayStreamBridge *bridge = cookie;

    airplay_stream_bridge_cancel(bridge);
    airplay_stream_bridge_release_reader(bridge);
    airplay_stream_bridge_release(bridge);
}

static int libmpv_airplay_stream_open(void *user_data, char *uri,
                                      mpv_stream_cb_info *info)
{
    AirPlayStreamBridge *bridge = NULL;

    (void)user_data;
    if (!uri || !info || strcmp(uri, PLAYER_LIBMPV_AIRPLAY_URI) != 0)
        return MPV_ERROR_LOADING_FAILED;

    mutexLock(&g_mutex);
    bridge = g_airplay_stream_bridge;
    if (bridge)
        airplay_stream_bridge_retain(bridge);
    mutexUnlock(&g_mutex);
    if (!bridge || !airplay_stream_bridge_claim_reader(bridge))
    {
        airplay_stream_bridge_release(bridge);
        return MPV_ERROR_LOADING_FAILED;
    }

    memset(info, 0, sizeof(*info));
    info->cookie = bridge;
    info->read_fn = libmpv_airplay_stream_read;
    info->close_fn = libmpv_airplay_stream_close;
    info->cancel_fn = libmpv_airplay_stream_cancel;
    return 0;
}

bool player_libmpv_set_airplay_stream_bridge(AirPlayStreamBridge *bridge)
{
    AirPlayStreamBridge *previous = NULL;
    int registration_rc = 0;
    bool registered_now = false;
    bool player_available = false;
    bool ok = true;

    libmpv_ensure_sync();
    mutexLock(&g_mutex);
    player_available = g_mpv != NULL;
    if (bridge && !g_airplay_stream_registered)
    {
        if (!player_available)
            ok = false;
        else
        {
            registration_rc = mpv_stream_cb_add_ro(
                g_mpv, "airplay", NULL, libmpv_airplay_stream_open);
            ok = registration_rc >= 0;
            if (ok)
            {
                g_airplay_stream_registered = true;
                registered_now = true;
            }
        }
    }
    if (ok)
    {
        if (bridge)
            airplay_stream_bridge_retain(bridge);
        previous = g_airplay_stream_bridge;
        g_airplay_stream_bridge = bridge;
    }
    mutexUnlock(&g_mutex);

    if (!ok)
    {
        log_error("[player-libmpv] AirPlay stream registration failed: %s\n",
                  player_available ? mpv_error_string(registration_rc)
                                   : "player unavailable");
        return false;
    }
    if (registered_now)
        log_info("[player-libmpv] AirPlay stream protocol registered on demand\n");

    if (previous)
    {
        airplay_stream_bridge_cancel(previous);
        airplay_stream_bridge_release(previous);
    }
    return true;
}

static int libmpv_double_to_ms(double value)
{
    if (value <= 0.0)
        return 0;
    return (int)(value * 1000.0 + 0.5);
}

static void libmpv_log_trace(const char *action, const char *phase, const char *detail, const char *uri)
{
    char summary[LIBMPV_TRACE_URL_MAX];
    uint32_t hash = uri && uri[0] ? player_trace_uri_hash(uri) : player_trace_current_media_hash();

    if (phase && strcmp(phase, "failed") == 0)
    {
        player_trace_warn("[media-trace] seq=%u t_ms=%llu layer=libmpv action=%s phase=%s url_hash=%08x detail=%s url=%s\n",
                          player_trace_current_media_seq(),
                          (unsigned long long)player_trace_elapsed_ms(),
                          action ? action : "(unknown)",
                          phase,
                          hash,
                          detail ? detail : "-",
                          player_trace_uri_summary(uri, summary, sizeof(summary)));
        return;
    }

    player_trace_log("[media-trace] seq=%u t_ms=%llu layer=libmpv action=%s phase=%s url_hash=%08x detail=%s url=%s\n",
                     player_trace_current_media_seq(),
                     (unsigned long long)player_trace_elapsed_ms(),
                     action ? action : "(unknown)",
                     phase ? phase : "(unknown)",
                     hash,
                     detail ? detail : "-",
                     player_trace_uri_summary(uri, summary, sizeof(summary)));
}

static void libmpv_process_volume_shutdown(void)
{
    if (!g_process_volume_active)
        return;

    audaExit();
    g_process_volume_active = false;
    g_process_volume_pid = 0;
}

static bool libmpv_process_volume_init(void)
{
    Result rc;
    u64 pid = 0;

    libmpv_process_volume_shutdown();

    if (!hosversionAtLeast(11, 0, 0))
        return false;

    rc = audaInitialize();
    if (R_FAILED(rc))
        return false;

    rc = svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
    if (R_FAILED(rc))
    {
        audaExit();
        return false;
    }

    g_process_volume_active = true;
    g_process_volume_pid = pid;
    return true;
}

static bool libmpv_process_volume_apply(int volume_0_100, bool mute)
{
    Result rc;
    float volume;

    if (!g_process_volume_active)
        return false;

    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;

    volume = mute ? 0.0f : (float)volume_0_100 / 100.0f;
    rc = audaSetAudioOutputProcessMasterVolume(g_process_volume_pid, 0, volume);
    if (R_FAILED(rc))
    {
        log_warn("[player-libmpv] aud:a set output volume failed: 0x%x\n", rc);
        libmpv_process_volume_shutdown();
        return false;
    }

    return true;
}

static const char *libmpv_log_noise_name(LibmpvLogNoise noise)
{
    switch (noise)
    {
    case LIBMPV_LOG_NOISE_HTTP_PREMATURE:
        return "premature HTTP stream";
    case LIBMPV_LOG_NOISE_H264:
        return "H.264 decode";
    case LIBMPV_LOG_NOISE_AAC:
        return "AAC decode";
    case LIBMPV_LOG_NOISE_DECODE:
        return "decode";
    case LIBMPV_LOG_NOISE_NONE:
    default:
        return "unknown";
    }
}

static LibmpvLogNoise libmpv_classify_noisy_log(const char *prefix, const char *text)
{
    bool is_ffmpeg = prefix && strstr(prefix, "ffmpeg");
    bool is_video_decoder = prefix && strcmp(prefix, "vd") == 0;
    bool is_audio_decoder = prefix && strcmp(prefix, "ad") == 0;

    if (!text)
        return LIBMPV_LOG_NOISE_NONE;

    if (is_ffmpeg && strstr(text, "Stream ends prematurely"))
        return LIBMPV_LOG_NOISE_HTTP_PREMATURE;

    if ((is_ffmpeg || is_video_decoder) &&
        (strstr(text, "Invalid NAL unit size") ||
         strstr(text, "Invalid NAL unit") ||
         strstr(text, "Error splitting the input into NAL units") ||
         strstr(text, "missing picture in access unit") ||
         strstr(text, "co located POCs unavailable") ||
         strstr(text, "mmco: unref short failure") ||
         strstr(text, "error while decoding MB")))
    {
        return LIBMPV_LOG_NOISE_H264;
    }

    if ((is_ffmpeg || is_audio_decoder) &&
        (strstr(text, "Error decoding audio") ||
         strstr(text, "Reserved bit set") ||
         strstr(text, "Prediction is not allowed in AAC-LC") ||
         strstr(text, "Number of bands") ||
         strstr(text, "Number of scalefactor bands") ||
         strstr(text, "channel element") ||
         strstr(text, "Input buffer exhausted") ||
         strstr(text, "invalid band type") ||
         strstr(text, "Sample rate index") ||
         strstr(text, "SBR was found") ||
         strstr(text, "ms_present") ||
         strstr(text, "Pulse ") ||
         strstr(text, "TNS filter order") ||
         strstr(text, "More than one AAC RDB") ||
         strstr(text, "Gain control") ||
         strstr(text, "Clipped noise gain") ||
         strstr(text, "If you heard an audible artifact") ||
         strstr(text, "If you want to help") ||
         strstr(text, "Invalid audio PTS")))
    {
        return LIBMPV_LOG_NOISE_AAC;
    }

    if ((is_ffmpeg || is_video_decoder || is_audio_decoder) &&
        (strstr(text, "Error while decoding frame") ||
         strstr(text, "Error decoding audio")))
    {
        return LIBMPV_LOG_NOISE_DECODE;
    }

    return LIBMPV_LOG_NOISE_NONE;
}

static void libmpv_reset_log_noise_locked(void)
{
    memset(g_log_noise_seen, 0, sizeof(g_log_noise_seen));
    memset(g_log_noise_suppressed, 0, sizeof(g_log_noise_suppressed));
}

static bool libmpv_should_suppress_log(const char *prefix, const char *text)
{
    LibmpvLogNoise noise = libmpv_classify_noisy_log(prefix, text);
    unsigned int seen;
    bool announce = false;
    bool suppress;

    if (noise == LIBMPV_LOG_NOISE_NONE)
        return false;

    mutexLock(&g_mutex);
    seen = ++g_log_noise_seen[noise];
    suppress = seen > LIBMPV_LOG_NOISE_KEEP;
    if (suppress)
    {
        ++g_log_noise_suppressed[noise];
        announce = seen == LIBMPV_LOG_NOISE_KEEP + 1u;
    }
    mutexUnlock(&g_mutex);

    if (announce)
    {
        log_warn("[player-libmpv] suppressing repeated %s log messages after %u entries\n",
                 libmpv_log_noise_name(noise),
                 LIBMPV_LOG_NOISE_KEEP);
    }

    return suppress;
}

static void libmpv_flush_log_noise_summary(void)
{
    unsigned int suppressed[LIBMPV_LOG_NOISE_COUNT];

    mutexLock(&g_mutex);
    memcpy(suppressed, g_log_noise_suppressed, sizeof(suppressed));
    libmpv_reset_log_noise_locked();
    mutexUnlock(&g_mutex);

    for (size_t i = 0; i < LIBMPV_LOG_NOISE_COUNT; ++i)
    {
        if (suppressed[i] == 0)
            continue;
        log_warn("[player-libmpv] suppressed %u repeated %s log messages\n",
                 suppressed[i],
                 libmpv_log_noise_name((LibmpvLogNoise)i));
    }
}

static void libmpv_set_option_string_checked(const char *name, const char *value)
{
    int rc;

    if (!g_mpv || !name || !value)
        return;

    rc = mpv_set_option_string(g_mpv, name, value);
    if (rc < 0)
    {
        log_warn("[player-libmpv] option %s=%s rejected: %s\n",
                 name,
                 value,
                 mpv_error_string(rc));
    }
}

static void libmpv_log_mpv_message(const mpv_event_log_message *msg)
{
    char *text;
    size_t len;

    if (!msg || !msg->text)
        return;

    libmpv_observe_mpv_log(msg);

    text = strdup(msg->text);
    if (!text)
        return;

    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = '\0';

    if (len == 0)
    {
        free(text);
        return;
    }

    if (libmpv_should_suppress_log(msg->prefix, text))
    {
        free(text);
        return;
    }

    if (msg->log_level <= MPV_LOG_LEVEL_ERROR)
        log_error("[player-libmpv][%s] %s\n", msg->prefix ? msg->prefix : "mpv", text);
    else if (msg->log_level <= MPV_LOG_LEVEL_WARN)
        log_warn("[player-libmpv][%s] %s\n", msg->prefix ? msg->prefix : "mpv", text);
#if defined(NXCAST_MEDIA_TRACE_VERBOSE) && NXCAST_MEDIA_TRACE_VERBOSE
    else if (msg->log_level <= MPV_LOG_LEVEL_INFO)
        log_info("[player-libmpv][%s] %s\n", msg->prefix ? msg->prefix : "mpv", text);
#endif
    else
        log_debug("[player-libmpv][%s] %s\n", msg->prefix ? msg->prefix : "mpv", text);

    free(text);
}

static char *libmpv_format_seek_target(int position_ms)
{
    int needed;
    char *target;

    needed = snprintf(NULL, 0, "%.3f", position_ms / 1000.0);
    if (needed < 0)
        return NULL;

    target = malloc((size_t)needed + 1);
    if (!target)
        return NULL;

    snprintf(target, (size_t)needed + 1, "%.3f", position_ms / 1000.0);
    return target;
}

static char *libmpv_normalize_seek_target_alloc(const char *target)
{
    int position_ms = 0;

    if (!target)
        return NULL;
    if (player_seek_target_parse_ms(target, &position_ms))
        return libmpv_format_seek_target(position_ms);
    return strdup(target);
}

static bool libmpv_set_uri_locked(const char *uri)
{
    char *copy = NULL;

    if (uri)
    {
        copy = strdup(uri);
        if (!copy)
            return false;
    }

    free(g_uri);
    g_uri = copy;
    return true;
}

static void libmpv_queue_event_locked(LibmpvPendingEvents *pending, PlayerEventType type)
{
    PlayerEvent *event = NULL;
    const char *uri = NULL;

    if (!pending || pending->count >= (sizeof(pending->events) / sizeof(pending->events[0])))
        return;

    event = &pending->events[pending->count++];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->state = g_state;
    event->position_ms = g_position_ms;
    event->duration_ms = g_duration_ms;
    event->volume = g_volume;
    event->mute = g_mute;
    event->seekable = g_seekable;
    event->error_code = g_last_error;
    uri = g_has_media ? g_uri : NULL;
    if (uri)
        event->uri = strdup(uri);
}

static void libmpv_flush_events(LibmpvPendingEvents *pending)
{
    if (!pending)
        return;

    for (size_t i = 0; i < pending->count; ++i)
    {
        if (g_event_sink)
            g_event_sink(&pending->events[i]);
        player_event_clear(&pending->events[i]);
    }
}

static PlayerState libmpv_derive_state_locked(void)
{
    if (g_last_error != 0)
        return PLAYER_STATE_ERROR;
    if (!g_has_media || !g_uri || g_uri[0] == '\0')
        return PLAYER_STATE_IDLE;
    if (g_stopped)
        return PLAYER_STATE_STOPPED;
    if (!g_file_loaded)
        return PLAYER_STATE_LOADING;
    if (g_startup_render_hold)
        return PLAYER_STATE_LOADING;
    if (g_seeking)
        return PLAYER_STATE_SEEKING;
    if (g_paused_for_cache && !g_pause)
        return PLAYER_STATE_BUFFERING;
    return g_pause ? PLAYER_STATE_PAUSED : PLAYER_STATE_PLAYING;
}

static void libmpv_refresh_state_locked(LibmpvPendingEvents *pending)
{
    PlayerState next_state = libmpv_derive_state_locked();
    if (next_state != g_state)
    {
        g_state = next_state;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_STATE_CHANGED);
    }
}

static void libmpv_set_position_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value != g_position_ms)
    {
        g_position_ms = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_POSITION_CHANGED);
    }
}

static void libmpv_set_duration_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value != g_duration_ms)
    {
        g_duration_ms = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_DURATION_CHANGED);
    }
}

static void libmpv_set_volume_locked(int value, LibmpvPendingEvents *pending)
{
    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    if (value != g_volume)
    {
        g_volume = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_VOLUME_CHANGED);
    }
}

static void libmpv_set_mute_locked(bool value, LibmpvPendingEvents *pending)
{
    if (value != g_mute)
    {
        g_mute = value;
        libmpv_queue_event_locked(pending, PLAYER_EVENT_MUTE_CHANGED);
    }
}

static void libmpv_set_seekable_locked(bool value)
{
    g_seekable = value;
}

static void libmpv_clear_pending_seek_locked(void)
{
    g_pending_seek = false;
    free(g_pending_seek_target);
    g_pending_seek_target = NULL;
}

static void libmpv_reset_startup_gate_locked(void)
{
    g_startup_render_hold = false;
    g_startup_restart_seen = false;
    g_initial_seek_inflight = false;
    g_initial_seek_event_seen = false;
    g_play_requested = false;
    g_play_unpause_pending = false;
    g_startup_hold_deadline_ms = 0;
}

static void libmpv_release_startup_gate_locked(const char *reason)
{
    if (!g_startup_render_hold)
        return;
    g_startup_render_hold = false;
    g_initial_seek_inflight = false;
    g_initial_seek_event_seen = false;
    g_startup_hold_deadline_ms = 0;
    libmpv_log_trace("startup-gate", "released", reason ? reason : "-", g_uri);
}

static bool libmpv_dispatch_play_locked(const char *detail)
{
    int pause_flag = 0;
    int rc;

    libmpv_log_trace("Play", "dispatch", detail ? detail : "-", g_uri);
    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_PLAY, "pause", MPV_FORMAT_FLAG, &pause_flag);
    if (rc < 0)
    {
        libmpv_log_trace("Play", "failed", mpv_error_string(rc), g_uri);
        log_warn("[player-libmpv] play failed: %s\n", mpv_error_string(rc));
        return false;
    }

    return true;
}

static void libmpv_reset_locked(void)
{
    free(g_uri);
    g_uri = NULL;
    g_has_media = false;
    g_state = PLAYER_STATE_IDLE;
    g_position_ms = 0;
    g_duration_ms = 0;
    g_volume = PLAYER_DEFAULT_VOLUME;
    g_mute = false;
    g_seekable = false;
    g_pause = true;
    g_paused_for_cache = false;
    g_seeking = false;
    g_pending_seek = false;
    free(g_pending_seek_target);
    g_pending_seek_target = NULL;
    g_stopped = false;
    g_file_loaded = false;
    g_load_pending = false;
    libmpv_reset_startup_gate_locked();
    g_last_error = 0;
    g_render_update_pending = false;
    g_render_backend = LIBMPV_RENDER_BACKEND_NONE;
    libmpv_reset_log_noise_locked();
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    g_dlna_observe_active = false;
    g_dlna_observe_last_ms = 0u;
    g_dlna_observe_http_status = -1;
    snprintf(g_dlna_observe_range_stage,
             sizeof(g_dlna_observe_range_stage), "unknown");
#endif
}

static bool libmpv_send_seek_target_locked(const char *target, LibmpvPendingEvents *pending)
{
    const char *args[4];
    char *normalized = NULL;
    int rc;

    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0' || g_state == PLAYER_STATE_STOPPED || !target || target[0] == '\0')
        return false;

    normalized = libmpv_normalize_seek_target_alloc(target);
    if (normalized && strcmp(normalized, target) != 0)
        log_debug("[player-libmpv] normalize seek target raw=%s normalized=%s\n", target, normalized);

    args[0] = "seek";
    args[1] = normalized ? normalized : target;
    args[2] = "absolute";
    args[3] = NULL;

    libmpv_log_trace("Seek", "dispatch", args[1], g_uri);
    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_SEEK, args);
    free(normalized);
    if (rc < 0)
    {
        libmpv_log_trace("Seek", "failed", mpv_error_string(rc), g_uri);
        log_warn("[player-libmpv] seek failed: %s\n", mpv_error_string(rc));
        return false;
    }

    g_last_error = 0;
    g_seeking = true;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    if (g_dlna_observe_active)
        snprintf(g_dlna_observe_range_stage,
                 sizeof(g_dlna_observe_range_stage), "seek-dispatch");
#endif
    libmpv_refresh_state_locked(pending);
    return true;
}

static bool libmpv_maybe_issue_pending_seek_locked(LibmpvPendingEvents *pending)
{
    const char *target;

    if (!g_pending_seek || !g_file_loaded)
        return false;

    if (!g_seekable)
        return false;

    target = g_pending_seek_target;
    if (libmpv_send_seek_target_locked(target, pending))
    {
        log_info("[player-libmpv] issue deferred seek target=%s loaded=%d seekable=%d\n",
                 target ? target : "(null)",
                 g_file_loaded ? 1 : 0,
                 g_seekable ? 1 : 0);
        libmpv_clear_pending_seek_locked();
        return true;
    }
    return false;
}

static bool libmpv_async_load_current(bool paused)
{
    const char *args[5];
    char options[256];
    int rc;
    LibmpvPendingEvents pending = {0};
    char detail[96];
    bool direct_mp4;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    PlayerOwnershipLease observe_lease = {0};
    bool observe_dlna =
        player_ownership_current(&observe_lease) &&
        observe_lease.owner == PLAYER_MEDIA_OWNER_DLNA;
#endif

    libmpv_log_resource_boundary("next-loadfile");

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    direct_mp4 = libmpv_is_direct_mp4(g_uri);
    if (direct_mp4)
    {
        snprintf(options,
                 sizeof(options),
                 "pause=%s,cache=yes,cache-pause-initial=no,demuxer-readahead-secs=2,demuxer-max-bytes=8MiB,demuxer-max-back-bytes=2MiB",
                 paused ? "yes" : "no");
    }
    else
    {
        snprintf(options, sizeof(options), "pause=%s", paused ? "yes" : "no");
    }

    args[0] = "loadfile";
    args[1] = g_uri;
    args[2] = "replace";
    args[3] = options;
    args[4] = NULL;

    libmpv_reset_log_noise_locked();

    snprintf(detail,
             sizeof(detail),
             "paused=%d profile=%s",
             paused ? 1 : 0,
             direct_mp4 ? "direct-mp4-fast" : "default-stable");
    libmpv_log_trace("loadfile", "dispatch", detail, g_uri);
    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_LOADFILE, args);
    if (rc < 0)
    {
        libmpv_log_trace("loadfile", "failed", mpv_error_string(rc), g_uri);
        log_error("[player-libmpv] loadfile failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_pause = paused;
    g_stopped = false;
    g_file_loaded = false;
    g_load_pending = true;
    g_startup_render_hold = true;
    g_startup_restart_seen = false;
    g_initial_seek_inflight = false;
    g_initial_seek_event_seen = false;
    g_play_requested = !paused;
    g_play_unpause_pending = false;
    g_startup_hold_deadline_ms = libmpv_monotonic_ms() + LIBMPV_STARTUP_GATE_TIMEOUT_MS;
    g_seeking = false;
    g_paused_for_cache = false;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    g_dlna_observe_active = observe_dlna;
    g_dlna_observe_last_ms = 0u;
    g_dlna_observe_http_status = -1;
    snprintf(g_dlna_observe_range_stage,
             sizeof(g_dlna_observe_range_stage), "loadfile");
#endif
    libmpv_clear_pending_seek_locked();
    libmpv_set_position_locked(0, &pending);
    libmpv_set_duration_locked(0, &pending);
    libmpv_refresh_state_locked(&pending);
    libmpv_queue_event_locked(&pending, PLAYER_EVENT_URI_CHANGED);
    libmpv_log_trace("loadfile", "queued", detail, g_uri);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static void libmpv_log_render_attach_order_warning(const char *backend_name)
{
    bool media_started;

    mutexLock(&g_mutex);
    media_started = g_has_media || g_file_loaded || g_state != PLAYER_STATE_IDLE;
    mutexUnlock(&g_mutex);

    if (media_started)
    {
        log_warn("[player-libmpv] attaching %s render context after media was set/started; libmpv render contexts are expected to be created before playback starts\n",
                 backend_name ? backend_name : "unknown");
    }
}

static bool libmpv_render_attach_context(mpv_render_context **out_ctx,
                                         mpv_handle *mpv,
                                         mpv_render_param *params,
                                         LibmpvRenderBackend backend,
                                         const char *backend_name)
{
    mpv_render_context *new_ctx = NULL;
    int rc;

    if (!out_ctx || !mpv || !params)
        return false;

    *out_ctx = NULL;

    rc = mpv_render_context_create(&new_ctx, mpv, params);
    if (rc < 0)
    {
        log_warn("[player-libmpv] render_context_create (%s) failed: %s\n",
                 backend_name ? backend_name : "unknown",
                 mpv_error_string(rc));
        return false;
    }

    mpv_render_context_set_update_callback(new_ctx, libmpv_on_render_update, NULL);

    mutexLock(&g_mutex);
    if (!g_render_ctx)
    {
        g_render_ctx = new_ctx;
        g_render_backend = backend;
        g_render_update_pending = true;
        new_ctx = NULL;
    }
    *out_ctx = g_render_ctx;
    mutexUnlock(&g_mutex);

    if (new_ctx)
        mpv_render_context_free(new_ctx);

    return *out_ctx != NULL;
}

static void libmpv_on_render_update(void *ctx)
{
    (void)ctx;
    mutexLock(&g_mutex);
    g_render_update_pending = true;
    mutexUnlock(&g_mutex);
}

static void libmpv_log_reply_error(uint64_t reply_userdata, int error)
{
    const char *name = "unknown";

    switch (reply_userdata)
    {
    case LIBMPV_REPLY_LOADFILE:
        name = "loadfile";
        break;
    case LIBMPV_REPLY_PLAY:
        name = "play";
        break;
    case LIBMPV_REPLY_PAUSE:
        name = "pause";
        break;
    case LIBMPV_REPLY_STOP:
        name = "stop";
        break;
    case LIBMPV_REPLY_SEEK:
        name = "seek";
        break;
    case LIBMPV_REPLY_SET_VOLUME:
        name = "set-volume";
        break;
    case LIBMPV_REPLY_SET_MUTE:
        name = "set-mute";
        break;
    default:
        break;
    }

    player_trace_warn("[media-trace] seq=%u t_ms=%llu layer=libmpv action=%s phase=reply-failed url_hash=%08x detail=%s\n",
                      player_trace_current_media_seq(),
                      (unsigned long long)player_trace_elapsed_ms(),
                      name,
                      player_trace_current_media_hash(),
                      mpv_error_string(error));
    log_warn("[player-libmpv] async reply failed op=%s error=%s\n",
             name,
             mpv_error_string(error));
}

static void libmpv_handle_property_change(const mpv_event_property *prop, uint64_t reply_userdata)
{
    LibmpvPendingEvents pending = {0};

    if (!prop)
        return;

    mutexLock(&g_mutex);
    switch (reply_userdata)
    {
    case LIBMPV_OBS_VOLUME:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_volume_locked((int)(*(double *)prop->data + 0.5), &pending);
        break;
    case LIBMPV_OBS_TIME_POS:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_position_locked(libmpv_double_to_ms(*(double *)prop->data), &pending);
        else if (prop->format == MPV_FORMAT_NONE)
            libmpv_set_position_locked(0, &pending);
        break;
    case LIBMPV_OBS_PAUSE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_pause = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    case LIBMPV_OBS_MUTE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            libmpv_set_mute_locked((*(int *)prop->data) != 0, &pending);
        break;
    case LIBMPV_OBS_DURATION:
        if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
            libmpv_set_duration_locked(libmpv_double_to_ms(*(double *)prop->data), &pending);
        else if (prop->format == MPV_FORMAT_NONE)
            libmpv_set_duration_locked(0, &pending);
        break;
    case LIBMPV_OBS_SEEKABLE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            libmpv_set_seekable_locked((*(int *)prop->data) != 0);
        libmpv_maybe_issue_pending_seek_locked(&pending);
        break;
    case LIBMPV_OBS_PAUSED_FOR_CACHE:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_paused_for_cache = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    case LIBMPV_OBS_SEEKING:
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            g_seeking = (*(int *)prop->data) != 0;
        libmpv_refresh_state_locked(&pending);
        break;
    default:
        break;
    }
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
}

static void libmpv_handle_event(mpv_event *event)
{
    LibmpvPendingEvents pending = {0};

    if (!event)
        return;

    switch (event->event_id)
    {
    case MPV_EVENT_START_FILE:
        libmpv_flush_log_noise_summary();
        libmpv_log_trace("mpv-start-file", "event", "-", g_uri);
        log_info("[player-libmpv] start-file uri=%s\n", g_uri ? g_uri : "(null)");
        mutexLock(&g_mutex);
        g_last_error = 0;
        g_stopped = false;
        g_file_loaded = false;
        g_load_pending = false;
        g_seeking = false;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_FILE_LOADED:
        libmpv_log_trace("mpv-file-loaded", "event", "-", g_uri);
        log_info("[player-libmpv] file-loaded uri=%s\n", g_uri ? g_uri : "(null)");
        mutexLock(&g_mutex);
        g_last_error = 0;
        g_stopped = false;
        g_file_loaded = true;
        libmpv_maybe_issue_pending_seek_locked(&pending);
        if (g_play_requested && g_play_unpause_pending)
        {
            g_play_unpause_pending = false;
            if (libmpv_dispatch_play_locked("file-loaded-autoplay"))
                g_pause = false;
        }
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_SEEK:
        mutexLock(&g_mutex);
        g_seeking = true;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
        if (g_dlna_observe_active)
            snprintf(g_dlna_observe_range_stage,
                     sizeof(g_dlna_observe_range_stage), "seek-event");
#endif
        if (g_startup_render_hold && g_initial_seek_inflight)
            g_initial_seek_event_seen = true;
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        libmpv_log_trace("mpv-playback-restart", "event", "-", g_uri);
        mutexLock(&g_mutex);
        g_file_loaded = true;
        g_stopped = false;
        g_seeking = false;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
        if (g_dlna_observe_active)
            snprintf(g_dlna_observe_range_stage,
                     sizeof(g_dlna_observe_range_stage), "restart");
#endif
        libmpv_maybe_issue_pending_seek_locked(&pending);
        if (g_startup_render_hold && g_play_requested)
        {
            if (g_initial_seek_inflight && g_initial_seek_event_seen)
            {
                libmpv_release_startup_gate_locked("initial-seek-complete");
            }
            else if (!g_startup_restart_seen)
            {
                g_startup_restart_seen = true;
                if (!g_initial_seek_inflight)
                {
                    g_startup_hold_deadline_ms = libmpv_monotonic_ms() + LIBMPV_INITIAL_SEEK_WINDOW_MS;
                    libmpv_log_trace("startup-gate", "seek-window", "open", g_uri);
                }
            }
            else if (!g_initial_seek_inflight)
            {
                libmpv_release_startup_gate_locked("second-playback-restart");
            }
        }
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    case MPV_EVENT_END_FILE:
    {
        mpv_event_end_file *end = (mpv_event_end_file *)event->data;
        const char *reason = "unknown";

        libmpv_flush_log_noise_summary();

        if (end)
        {
            switch (end->reason)
            {
            case MPV_END_FILE_REASON_EOF:
                reason = "eof";
                break;
            case MPV_END_FILE_REASON_STOP:
                reason = "stop";
                break;
            case MPV_END_FILE_REASON_QUIT:
                reason = "quit";
                break;
            case MPV_END_FILE_REASON_ERROR:
                reason = "error";
                break;
            case MPV_END_FILE_REASON_REDIRECT:
                reason = "redirect";
                break;
            default:
                break;
            }
        }
        mutexLock(&g_mutex);
        bool replaced_previous = g_load_pending;
        mutexUnlock(&g_mutex);
        if (replaced_previous)
        {
            libmpv_log_resource_boundary("end-file-replaced");
            libmpv_log_trace("mpv-end-file", "event", "replaced-previous", g_uri);
            log_info("[player-libmpv] ignored end-file for replaced media reason=%s\n", reason);
            break;
        }

        log_info("[player-libmpv] end-file uri=%s reason=%s error=%s\n",
                 g_uri ? g_uri : "(null)",
                 reason,
                 end ? mpv_error_string(end->error) : "success");
        libmpv_log_trace("mpv-end-file", "event", reason, g_uri);
        libmpv_log_resource_boundary("end-file");

        mutexLock(&g_mutex);
        g_stopped = true;
        g_file_loaded = false;
        libmpv_reset_startup_gate_locked();
        g_seeking = false;
        libmpv_clear_pending_seek_locked();
        g_paused_for_cache = false;
        g_pause = true;

        if (end && end->reason == MPV_END_FILE_REASON_ERROR)
        {
            g_last_error = end->error;
            libmpv_refresh_state_locked(&pending);
            libmpv_queue_event_locked(&pending, PLAYER_EVENT_ERROR);
        }
        else
        {
            g_last_error = 0;
            if (end && end->reason == MPV_END_FILE_REASON_EOF && g_duration_ms > 0)
                libmpv_set_position_locked(g_duration_ms, &pending);
            else
                libmpv_set_position_locked(0, &pending);
            libmpv_refresh_state_locked(&pending);
        }
        mutexUnlock(&g_mutex);

        libmpv_flush_events(&pending);
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE:
        libmpv_handle_property_change((mpv_event_property *)event->data, event->reply_userdata);
        break;
    case MPV_EVENT_COMMAND_REPLY:
    case MPV_EVENT_SET_PROPERTY_REPLY:
        if (event->error < 0)
        {
            mutexLock(&g_mutex);
            if (event->reply_userdata == LIBMPV_REPLY_SEEK)
            {
                g_seeking = false;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
                if (g_dlna_observe_active)
                    snprintf(g_dlna_observe_range_stage,
                             sizeof(g_dlna_observe_range_stage),
                             "seek-failed");
#endif
            }
            if (event->reply_userdata == LIBMPV_REPLY_LOADFILE)
                g_load_pending = false;
            libmpv_refresh_state_locked(&pending);
            mutexUnlock(&g_mutex);
            libmpv_log_reply_error(event->reply_userdata, event->error);
            libmpv_flush_events(&pending);
        }
        break;
    case MPV_EVENT_LOG_MESSAGE:
        libmpv_log_mpv_message((mpv_event_log_message *)event->data);
        break;
    case MPV_EVENT_SHUTDOWN:
        mutexLock(&g_mutex);
        g_stopped = true;
        g_file_loaded = false;
        libmpv_reset_startup_gate_locked();
        g_seeking = false;
        libmpv_clear_pending_seek_locked();
        libmpv_refresh_state_locked(&pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        break;
    default:
        break;
    }
}

static bool libmpv_available(void)
{
    return true;
}

static bool libmpv_init(void)
{
    int rc;
    const char *mpv_log_level;
    bool process_volume = false;
    double neutral_volume = 100.0;
    int neutral_mute = 0;

    libmpv_ensure_sync();
    libmpv_process_volume_shutdown();
    mutexLock(&g_mutex);
    libmpv_reset_locked();
    mutexUnlock(&g_mutex);

    g_mpv = mpv_create();
    if (!g_mpv)
    {
        log_error("[player-libmpv] mpv_create failed\n");
        return false;
    }

    libmpv_set_option_string_checked("config", "no");
    libmpv_set_option_string_checked("terminal", "no");
    libmpv_set_option_string_checked("input-default-bindings", "no");
    libmpv_set_option_string_checked("input-vo-keyboard", "no");
    libmpv_set_option_string_checked("osc", "no");
    libmpv_set_option_string_checked("osd-level", "0");
    libmpv_set_option_string_checked("audio-display", "no");
    libmpv_set_option_string_checked("image-display-duration", "inf");
    libmpv_set_option_string_checked("idle", "yes");
    // Switch libass builds often lack fontconfig/coretext providers.
    // Disable provider probing to avoid startup warnings for an OSD path we do not use.
    libmpv_set_option_string_checked("sub-font-provider", "none");
    // Prefer hardware decode on Switch. If the libmpv package exposes the
    // explicit nvtegra backend, use it directly; otherwise fall back to mpv's
    // generic enabled path and let the selected render backend negotiate interop.
#ifdef HAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
    libmpv_set_option_string_checked("hwdec", "nvtegra");
#else
    libmpv_set_option_string_checked("hwdec", "yes");
#endif
    // This is the v0.2.0-tested nvtegra/deko3d surface handoff policy.
    libmpv_set_option_string_checked("vd-lavc-dr", "no");
    libmpv_set_option_string_checked("vd-lavc-threads", "4");
    libmpv_set_option_string_checked("gpu-hwdec-interop", "auto");
    libmpv_set_option_string_checked("vo", "libmpv");
    libmpv_set_option_string_checked("audio-channels", "stereo");
    libmpv_set_option_string_checked("audio-client-name", "NX-Cast");
    libmpv_set_option_string_checked("ao", "hos");

    rc = mpv_initialize(g_mpv);
    if (rc < 0)
    {
        log_error("[player-libmpv] mpv_initialize failed: %s\n", mpv_error_string(rc));
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
        return false;
    }

    process_volume = libmpv_process_volume_init();
    if (process_volume)
    {
        (void)mpv_set_property(g_mpv, "volume", MPV_FORMAT_DOUBLE, &neutral_volume);
        (void)mpv_set_property(g_mpv, "mute", MPV_FORMAT_FLAG, &neutral_mute);
        if (!libmpv_process_volume_apply(PLAYER_DEFAULT_VOLUME, false))
            process_volume = false;
    }

#if defined(NXCAST_MEDIA_TRACE_VERBOSE) && NXCAST_MEDIA_TRACE_VERBOSE
    mpv_log_level = "info";
#else
    mpv_log_level = log_get_mpv_level();
#endif
    rc = mpv_request_log_messages(g_mpv, mpv_log_level);
    if (rc < 0)
        log_warn("[player-libmpv] log subscription level=%s failed: %s\n",
                 mpv_log_level, mpv_error_string(rc));
    else
        log_info("[player-libmpv] log subscription level=%s\n",
                 mpv_log_level);
    if (!process_volume)
        mpv_observe_property(g_mpv, LIBMPV_OBS_VOLUME, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSE, "pause", MPV_FORMAT_FLAG);
    if (!process_volume)
        mpv_observe_property(g_mpv, LIBMPV_OBS_MUTE, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKABLE, "seekable", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_PAUSED_FOR_CACHE, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, LIBMPV_OBS_SEEKING, "seeking", MPV_FORMAT_FLAG);

    if (process_volume)
        log_info("[player-libmpv] init volume_backend=aud:a-process\n");
    log_info("[player-libmpv] init\n");
    return true;
}

static void libmpv_deinit(void)
{
    mpv_render_context *render_ctx = NULL;
    mpv_handle *mpv = NULL;
    AirPlayStreamBridge *airplay_bridge = NULL;

    log_info("[player-libmpv] deinit begin\n");
    libmpv_flush_log_noise_summary();

    mutexLock(&g_mutex);
    render_ctx = g_render_ctx;
    g_render_ctx = NULL;
    mpv = g_mpv;
    g_mpv = NULL;
    airplay_bridge = g_airplay_stream_bridge;
    g_airplay_stream_bridge = NULL;
    g_airplay_stream_registered = false;
    libmpv_reset_locked();
    mutexUnlock(&g_mutex);

    if (airplay_bridge)
        airplay_stream_bridge_cancel(airplay_bridge);

    if (render_ctx)
    {
        log_info("[player-libmpv] deinit step=render_context_free begin\n");
        mpv_render_context_free(render_ctx);
        log_info("[player-libmpv] deinit step=render_context_free done\n");
    }
    
    if (mpv)
    {
        log_info("[player-libmpv] deinit step=terminate_destroy begin\n");
        mpv_terminate_destroy(mpv);
        log_info("[player-libmpv] deinit step=terminate_destroy done\n");
    }

    airplay_stream_bridge_release(airplay_bridge);

    libmpv_process_volume_shutdown();

    log_info("[player-libmpv] deinit done\n");
}

static void libmpv_set_event_sink(void (*sink)(const PlayerEvent *event))
{
    g_event_sink = sink;
}

static bool libmpv_set_media(const PlayerMedia *media)
{
    char *previous_uri = NULL;
    bool previous_has_media;
    int previous_last_error;

    if (!media || !media->uri || media->uri[0] == '\0')
        return false;

    libmpv_log_trace("set_media", "begin", "-", media->uri);
    libmpv_log_trace("set_media", "mutex-wait", "-", media->uri);
    mutexLock(&g_mutex);
    libmpv_log_trace("set_media", "mutex-acquired", "-", media->uri);
    previous_uri = g_uri;
    previous_has_media = g_has_media;
    previous_last_error = g_last_error;

    g_uri = NULL;
    if (!libmpv_set_uri_locked(media->uri))
    {
        g_uri = previous_uri;
        mutexUnlock(&g_mutex);
        libmpv_log_trace("set_media", "failed", "copy-uri", media->uri);
        return false;
    }
    g_has_media = true;
    g_last_error = 0;
    mutexUnlock(&g_mutex);

    if (libmpv_async_load_current(true))
    {
        free(previous_uri);
        libmpv_log_trace("set_media", "done", "-", media->uri);
        return true;
    }

    mutexLock(&g_mutex);
    free(g_uri);
    g_uri = previous_uri;
    g_has_media = previous_has_media;
    g_last_error = previous_last_error;
    mutexUnlock(&g_mutex);
    libmpv_log_trace("set_media", "failed", "loadfile", media->uri);
    return false;
}

static bool libmpv_play(void)
{
    LibmpvPendingEvents pending = {0};

    libmpv_log_trace("Play", "mutex-wait", "-", NULL);
    mutexLock(&g_mutex);
    libmpv_log_trace("Play", "mutex-acquired", "-", g_uri);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    if (g_state == PLAYER_STATE_STOPPED)
    {
        mutexUnlock(&g_mutex);
        libmpv_log_trace("Play", "reload-stopped", "-", NULL);
        return libmpv_async_load_current(false);
    }

    g_last_error = 0;
    g_play_requested = true;
    if (g_startup_render_hold)
        g_startup_hold_deadline_ms = libmpv_monotonic_ms() + LIBMPV_STARTUP_GATE_TIMEOUT_MS;

    // loadfile applies its per-file pause option asynchronously. Defer the
    // unpause until FILE_LOADED so pause=yes cannot overwrite autoplay.
    if (!g_file_loaded)
    {
        g_play_unpause_pending = true;
        libmpv_log_trace("Play", "deferred", "waiting-file-loaded", g_uri);
    }
    else
    {
        g_play_unpause_pending = false;
        if (!libmpv_dispatch_play_locked("immediate"))
        {
            mutexUnlock(&g_mutex);
            return false;
        }
        g_pause = false;
    }
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_pause(void)
{
    int pause_flag = 1;
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0' || g_state == PLAYER_STATE_STOPPED)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_log_trace("Pause", "dispatch", "-", g_uri);
    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_PAUSE, "pause", MPV_FORMAT_FLAG, &pause_flag);
    if (rc < 0)
    {
        libmpv_log_trace("Pause", "failed", mpv_error_string(rc), g_uri);
        log_warn("[player-libmpv] pause failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    libmpv_release_startup_gate_locked("explicit-pause");
    g_play_requested = false;
    g_play_unpause_pending = false;
    g_pause = true;
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_stop(void)
{
    const char *args[] = {"stop", NULL};
    int rc;
    LibmpvPendingEvents pending = {0};

    libmpv_log_trace("Stop", "mutex-wait", "-", NULL);
    mutexLock(&g_mutex);
    libmpv_log_trace("Stop", "mutex-acquired", "-", g_uri);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_log_trace("Stop", "dispatch", "-", g_uri);
    rc = mpv_command_async(g_mpv, LIBMPV_REPLY_STOP, args);
    if (rc < 0)
    {
        libmpv_log_trace("Stop", "failed", mpv_error_string(rc), g_uri);
        log_warn("[player-libmpv] stop failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    g_last_error = 0;
    g_stopped = true;
    g_file_loaded = false;
    g_load_pending = false;
    libmpv_reset_startup_gate_locked();
    g_seeking = false;
    g_paused_for_cache = false;
    g_play_requested = false;
    g_pause = true;
    libmpv_clear_pending_seek_locked();
    libmpv_set_position_locked(0, &pending);
    libmpv_refresh_state_locked(&pending);
    mutexUnlock(&g_mutex);

    libmpv_log_resource_boundary("stop");
    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_seek_target(const char *target)
{
    LibmpvPendingEvents pending = {0};
    bool merge_initial_seek;

    mutexLock(&g_mutex);
    if (!g_mpv || !g_has_media || !g_uri || g_uri[0] == '\0' || g_state == PLAYER_STATE_STOPPED || !target || target[0] == '\0')
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    merge_initial_seek = g_startup_render_hold;
    if (merge_initial_seek)
    {
        g_initial_seek_inflight = true;
        g_initial_seek_event_seen = false;
        g_startup_hold_deadline_ms = libmpv_monotonic_ms() + LIBMPV_INITIAL_SEEK_TIMEOUT_MS;
        libmpv_log_trace("Seek", "initial-merge", target, g_uri);
    }

    if (!g_file_loaded)
    {
        char *copy = strdup(target);
        if (!copy)
        {
            mutexUnlock(&g_mutex);
            return false;
        }
        free(g_pending_seek_target);
        g_pending_seek = true;
        g_pending_seek_target = copy;
        log_info("[player-libmpv] defer seek target=%s loaded=%d seekable=%d\n",
                 target,
                 g_file_loaded ? 1 : 0,
                 g_seekable ? 1 : 0);
        mutexUnlock(&g_mutex);
        return true;
    }

    if (!libmpv_send_seek_target_locked(target, &pending))
    {
        if (merge_initial_seek)
        {
            g_initial_seek_inflight = false;
            g_startup_hold_deadline_ms = libmpv_monotonic_ms() + LIBMPV_INITIAL_SEEK_WINDOW_MS;
        }
        mutexUnlock(&g_mutex);
        return false;
    }
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_seek_ms(int position_ms)
{
    bool ok;
    char *target;

    if (position_ms < 0)
        position_ms = 0;

    target = libmpv_format_seek_target(position_ms);
    if (!target)
        return false;

    ok = libmpv_seek_target(target);
    free(target);
    return ok;
}

static bool libmpv_set_volume(int volume_0_100)
{
    double volume;
    int rc;
    LibmpvPendingEvents pending = {0};

    if (volume_0_100 < 0)
        volume_0_100 = 0;
    if (volume_0_100 > 100)
        volume_0_100 = 100;
    volume = (double)volume_0_100;

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    if (g_process_volume_active && libmpv_process_volume_apply(volume_0_100, g_mute))
    {
        libmpv_set_volume_locked(volume_0_100, &pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        return true;
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_SET_VOLUME, "volume", MPV_FORMAT_DOUBLE, &volume);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_volume failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_set_volume_locked(volume_0_100, &pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_set_mute(bool mute)
{
    int mute_flag = mute ? 1 : 0;
    int rc;
    LibmpvPendingEvents pending = {0};

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    if (g_process_volume_active && libmpv_process_volume_apply(g_volume, mute))
    {
        libmpv_set_mute_locked(mute, &pending);
        mutexUnlock(&g_mutex);
        libmpv_flush_events(&pending);
        return true;
    }

    rc = mpv_set_property_async(g_mpv, LIBMPV_REPLY_SET_MUTE, "mute", MPV_FORMAT_FLAG, &mute_flag);
    if (rc < 0)
    {
        log_warn("[player-libmpv] set_mute failed: %s\n", mpv_error_string(rc));
        mutexUnlock(&g_mutex);
        return false;
    }

    libmpv_set_mute_locked(mute, &pending);
    mutexUnlock(&g_mutex);

    libmpv_flush_events(&pending);
    return true;
}

static bool libmpv_show_osd(const char *text, int duration_ms)
{
    char duration_buf[16];
    const char *args[4];
    int rc;

    if (!text)
        return false;

    if (duration_ms < 0)
        duration_ms = 0;

    snprintf(duration_buf, sizeof(duration_buf), "%d", duration_ms);

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }

    args[0] = "show-text";
    args[1] = text;
    args[2] = duration_buf;
    args[3] = NULL;
    rc = mpv_command_async(g_mpv, 0, args);
    mutexUnlock(&g_mutex);

    if (rc < 0)
    {
        log_warn("[player-libmpv] show_osd failed: %s\n", mpv_error_string(rc));
        return false;
    }

    return true;
}

static void libmpv_release_expired_startup_gate(void)
{
    LibmpvPendingEvents pending = {0};
    const char *reason = NULL;
    uint64_t now_ms = libmpv_monotonic_ms();

    mutexLock(&g_mutex);
    if (g_startup_render_hold &&
        g_startup_hold_deadline_ms > 0 &&
        now_ms >= g_startup_hold_deadline_ms)
    {
        if (!g_file_loaded)
        {
            g_startup_hold_deadline_ms = now_ms + LIBMPV_STARTUP_GATE_TIMEOUT_MS;
            mutexUnlock(&g_mutex);
            return;
        }
        reason = g_initial_seek_inflight ? "initial-seek-timeout" :
                 (g_startup_restart_seen ? "initial-seek-window-expired" : "startup-timeout");
        libmpv_release_startup_gate_locked(reason);
        libmpv_refresh_state_locked(&pending);
    }
    mutexUnlock(&g_mutex);
    libmpv_flush_events(&pending);
}

static bool libmpv_pump_events(int timeout_ms)
{
    mpv_event *event;
    double timeout_s = timeout_ms <= 0 ? 0.0 : (double)timeout_ms / 1000.0;
    bool handled = false;

    mutexLock(&g_mutex);
    if (!g_mpv)
    {
        mutexUnlock(&g_mutex);
        return false;
    }
    mutexUnlock(&g_mutex);

    event = mpv_wait_event(g_mpv, timeout_s);
    while (event && event->event_id != MPV_EVENT_NONE)
    {
        handled = true;
        libmpv_handle_event(event);
        event = mpv_wait_event(g_mpv, 0.0);
    }

    libmpv_release_expired_startup_gate();
    libmpv_observe_dlna_sample();

    return handled;
}

static void libmpv_wakeup(void)
{
    mutexLock(&g_mutex);
    if (g_mpv)
        mpv_wakeup(g_mpv);
    mutexUnlock(&g_mutex);
}

static bool libmpv_render_supported(void)
{
    bool supported;

    mutexLock(&g_mutex);
    supported = g_mpv != NULL;
    mutexUnlock(&g_mutex);
    return supported;
}

#ifdef HAVE_MPV_RENDER_GL
static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    mpv_opengl_init_params gl_init_params;
    mpv_render_param params[4];
    mpv_handle *mpv = NULL;
    int advanced_control = 1;

    if (!get_proc_address)
        return false;

    mutexLock(&g_mutex);
    if (g_render_ctx)
    {
        bool ok = g_render_backend == LIBMPV_RENDER_BACKEND_GL;
        mutexUnlock(&g_mutex);
        return ok;
    }
    mpv = g_mpv;
    mutexUnlock(&g_mutex);
    if (!mpv)
        return false;

    libmpv_log_render_attach_order_warning("OpenGL");

    gl_init_params.get_proc_address = get_proc_address;
    gl_init_params.get_proc_address_ctx = get_proc_address_ctx;

    params[0].type = MPV_RENDER_PARAM_API_TYPE;
    params[0].data = (void *)MPV_RENDER_API_TYPE_OPENGL;
    params[1].type = MPV_RENDER_PARAM_OPENGL_INIT_PARAMS;
    params[1].data = &gl_init_params;
    params[2].type = MPV_RENDER_PARAM_ADVANCED_CONTROL;
    params[2].data = &advanced_control;
    params[3].type = MPV_RENDER_PARAM_INVALID;
    params[3].data = NULL;

    return libmpv_render_attach_context(&g_render_ctx, mpv, params, LIBMPV_RENDER_BACKEND_GL, "OpenGL");
}
#else
static bool libmpv_render_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx)
{
    (void)get_proc_address;
    (void)get_proc_address_ctx;
    log_warn("[player-libmpv] OpenGL render support not available at compile time\n");
    return false;
}
#endif

static bool libmpv_render_attach_sw(void)
{
    return false;
}

static void libmpv_render_detach(void)
{
    mpv_render_context *render_ctx = NULL;

    mutexLock(&g_mutex);
    render_ctx = g_render_ctx;
    g_render_ctx = NULL;
    g_render_update_pending = false;
    g_render_backend = LIBMPV_RENDER_BACKEND_NONE;
    mutexUnlock(&g_mutex);

    if (render_ctx)
        mpv_render_context_free(render_ctx);
}

#ifdef HAVE_MPV_RENDER_GL
static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    mpv_render_context *render_ctx = NULL;
    mpv_opengl_fbo mpv_fbo;
    mpv_render_param params[3];
    int flip = flip_y ? 1 : 0;
    uint64_t flags;
    int rc;

    if (width <= 0 || height <= 0)
        return false;

    mutexLock(&g_mutex);
    render_ctx = (g_render_backend == LIBMPV_RENDER_BACKEND_GL) ? g_render_ctx : NULL;
    g_render_update_pending = false;
    mutexUnlock(&g_mutex);
    if (!render_ctx)
        return false;

    flags = mpv_render_context_update(render_ctx);
    (void)flags;

    mpv_fbo.fbo = fbo;
    mpv_fbo.w = width;
    mpv_fbo.h = height;
    mpv_fbo.internal_format = 0;

    params[0].type = MPV_RENDER_PARAM_OPENGL_FBO;
    params[0].data = &mpv_fbo;
    params[1].type = MPV_RENDER_PARAM_FLIP_Y;
    params[1].data = &flip;
    params[2].type = MPV_RENDER_PARAM_INVALID;
    params[2].data = NULL;

    rc = mpv_render_context_render(render_ctx, params);
    return rc >= 0;
}
#else
static bool libmpv_render_frame_gl(int fbo, int width, int height, bool flip_y)
{
    (void)fbo;
    (void)width;
    (void)height;
    (void)flip_y;
    return false;
}
#endif

static bool libmpv_render_frame_sw(void *pixels, int width, int height, size_t stride)
{
    (void)pixels;
    (void)width;
    (void)height;
    (void)stride;
    return false;
}

static bool libmpv_render_attach_dk3d(const PlayerVideoDk3dInit *init)
{
#ifdef HAVE_MPV_RENDER_DK3D
    mpv_render_param params[4];
    mpv_handle *mpv = NULL;
    int advanced_control = 1;
    mpv_deko3d_init_params init_params;

    mutexLock(&g_mutex);
    if (g_render_ctx)
    {
        bool ok = g_render_backend == LIBMPV_RENDER_BACKEND_DK3D;
        mutexUnlock(&g_mutex);
        return ok;
    }
    mpv = g_mpv;
    mutexUnlock(&g_mutex);
    if (!mpv)
        return false;
    if (!init || !init->device)
    {
        log_warn("[player-libmpv] deko3d render requested without a valid device\n");
        return false;
    }

    memset(&init_params, 0, sizeof(init_params));
    init_params.device = init->device;

    libmpv_log_render_attach_order_warning("deko3d");

    params[0].type = MPV_RENDER_PARAM_API_TYPE;
    params[0].data = (void *)MPV_RENDER_API_TYPE_DEKO3D;
    params[1].type = MPV_RENDER_PARAM_DEKO3D_INIT_PARAMS;
    params[1].data = &init_params;
    params[2].type = MPV_RENDER_PARAM_ADVANCED_CONTROL;
    params[2].data = &advanced_control;
    params[3].type = MPV_RENDER_PARAM_INVALID;
    params[3].data = NULL;

    return libmpv_render_attach_context(&g_render_ctx, mpv, params, LIBMPV_RENDER_BACKEND_DK3D, "deko3d");
#else
    log_warn("[player-libmpv] deko3d render not available at compile time\n");
    return false;
#endif
}

static bool libmpv_render_frame_dk3d(const PlayerVideoDk3dFrame *frame)
{
    mpv_render_context *render_ctx = NULL;
    mpv_deko3d_fbo fbo;
    mpv_render_param params[2];
    uint64_t flags;
    int rc;

    if (!frame || !frame->image || !frame->ready_fence || !frame->done_fence ||
        frame->width <= 0 || frame->height <= 0)
        return false;

    mutexLock(&g_mutex);
    render_ctx = (g_render_backend == LIBMPV_RENDER_BACKEND_DK3D) ? g_render_ctx : NULL;
    g_render_update_pending = false;
    mutexUnlock(&g_mutex);
    if (!render_ctx)
        return false;

    flags = mpv_render_context_update(render_ctx);
    (void)flags;

    memset(&fbo, 0, sizeof(fbo));
    fbo.tex = frame->image;
    fbo.ready_fence = frame->ready_fence;
    fbo.done_fence = frame->done_fence;
    fbo.w = frame->width;
    fbo.h = frame->height;
    fbo.format = frame->format;

    params[0].type = MPV_RENDER_PARAM_DEKO3D_FBO;
    params[0].data = &fbo;
    params[1].type = MPV_RENDER_PARAM_INVALID;
    params[1].data = NULL;

    rc = mpv_render_context_render(render_ctx, params);
    return rc >= 0;
}

static int libmpv_get_position_ms(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_position_ms;
    mutexUnlock(&g_mutex);
    return value;
}

static int libmpv_get_duration_ms(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_duration_ms;
    mutexUnlock(&g_mutex);
    return value;
}

static int libmpv_get_volume(void)
{
    int value;

    mutexLock(&g_mutex);
    value = g_volume;
    mutexUnlock(&g_mutex);
    return value;
}

static bool libmpv_get_mute(void)
{
    bool value;

    mutexLock(&g_mutex);
    value = g_mute;
    mutexUnlock(&g_mutex);
    return value;
}

static bool libmpv_is_seekable(void)
{
    bool value;

    mutexLock(&g_mutex);
    value = g_seekable;
    mutexUnlock(&g_mutex);
    return value;
}

static PlayerState libmpv_get_state(void)
{
    PlayerState value;

    mutexLock(&g_mutex);
    value = g_state;
    mutexUnlock(&g_mutex);
    return value;
}

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
    .seek_target = libmpv_seek_target,
    .seek_ms = libmpv_seek_ms,
    .set_volume = libmpv_set_volume,
    .set_mute = libmpv_set_mute,
    .show_osd = libmpv_show_osd,
    .pump_events = libmpv_pump_events,
    .wakeup = libmpv_wakeup,
    .render_supported = libmpv_render_supported,
    .render_attach_gl = libmpv_render_attach_gl,
    .render_attach_sw = libmpv_render_attach_sw,
    .render_attach_dk3d = libmpv_render_attach_dk3d,
    .render_detach = libmpv_render_detach,
    .render_frame_gl = libmpv_render_frame_gl,
    .render_frame_sw = libmpv_render_frame_sw,
    .render_frame_dk3d = libmpv_render_frame_dk3d,
    .get_position_ms = libmpv_get_position_ms,
    .get_duration_ms = libmpv_get_duration_ms,
    .get_volume = libmpv_get_volume,
    .get_mute = libmpv_get_mute,
    .is_seekable = libmpv_is_seekable,
    .get_state = libmpv_get_state
};

#else

static bool libmpv_unavailable(void)
{
    return false;
}

bool player_libmpv_set_airplay_stream_bridge(AirPlayStreamBridge *bridge)
{
    (void)bridge;
    return false;
}

const BackendOps g_libmpv_ops = {
    .name = "libmpv",
    .available = libmpv_unavailable
};

#endif
