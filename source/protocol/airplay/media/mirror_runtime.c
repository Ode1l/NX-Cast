#include "mirror_runtime.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex AirPlayRuntimeMutex;
typedef CondVar AirPlayRuntimeCond;
typedef Thread AirPlayRuntimeThread;
typedef void (*AirPlayRuntimeThreadEntry)(void *argument);
#define AIRPLAY_RUNTIME_THREAD_RETURN void
#define AIRPLAY_RUNTIME_THREAD_FINISH() return
#define AIRPLAY_RUNTIME_THREAD_STACK_SIZE 0x10000u
#else
#include <pthread.h>
typedef pthread_mutex_t AirPlayRuntimeMutex;
typedef pthread_cond_t AirPlayRuntimeCond;
typedef pthread_t AirPlayRuntimeThread;
typedef void *(*AirPlayRuntimeThreadEntry)(void *argument);
#define AIRPLAY_RUNTIME_THREAD_RETURN void *
#define AIRPLAY_RUNTIME_THREAD_FINISH() return NULL
#endif

#include "protocol/airplay/mirror/mirror_session.h"
#include "protocol/airplay/mirror/audio.h"
#include "protocol/airplay/mirror/timing.h"
#include "protocol/airplay/diagnostics.h"
#include "protocol/airplay/trace.h"
#include "player/backend/libmpv_airplay.h"

#define AIRPLAY_RUNTIME_COMMAND_CAPACITY 32u

typedef enum
{
    AIRPLAY_RUNTIME_COMMAND_LOAD = 0,
    AIRPLAY_RUNTIME_COMMAND_REPLACE,
    AIRPLAY_RUNTIME_COMMAND_PLAY,
    AIRPLAY_RUNTIME_COMMAND_STOP,
    AIRPLAY_RUNTIME_COMMAND_AUDIO,
    AIRPLAY_RUNTIME_COMMAND_NOTIFY
} AirPlayRuntimeCommandType;

typedef struct
{
    AirPlayRuntimeCommandType type;
    uint32_t generation;
    AirPlayMirrorRuntimeStatus status;
    AirPlayStreamBridge *bridge;
    uint32_t previous_generation;
} AirPlayRuntimeCommand;

struct AirPlayMirrorRuntime
{
    AirPlayMirrorRuntimeConfig config;
    AirPlayRuntimeMutex mutex;
    AirPlayRuntimeCond command_ready;
    AirPlayRuntimeThread thread;
    AirPlayRuntimeCommand commands[AIRPLAY_RUNTIME_COMMAND_CAPACITY];
    size_t command_read;
    size_t command_count;
    AirPlayMirrorSession *mirror;
    AirPlayMirrorAudio *audio;
    AirPlayMirrorAudioFormat audio_format;
    AirPlayMirrorTiming *timing;
    AirPlayStreamBridge *bridge;
    AirPlayStreamBridge *audio_bridge;
    uint64_t transport_session_id;
    uint64_t session_id;
    uint32_t generation;
    AirPlayMirrorRuntimeStatus status;
    bool opening;
    bool audio_format_ready;
    bool recording;
    bool player_load_queued;
    bool player_play_queued;
    bool audio_sink_reported;
    bool audio_attach_queued;
    bool audio_mux_failure_reported;
    bool uses_ntp_timing;
    bool audio_sync_reported;
    atomic_bool running;
    bool thread_started;
    uint32_t diagnostic_thread_generation;
    uint64_t diagnostic_last_video_ms;
    uint64_t diagnostic_audio_packets;
    uint64_t diagnostic_audio_frames;
};

static bool runtime_mutex_init(AirPlayRuntimeMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void runtime_mutex_destroy(AirPlayRuntimeMutex *mutex)
{
#ifndef __SWITCH__
    pthread_mutex_destroy(mutex);
#else
    (void)mutex;
#endif
}

static void runtime_mutex_lock(AirPlayRuntimeMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void runtime_mutex_unlock(AirPlayRuntimeMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

static bool runtime_cond_init(AirPlayRuntimeCond *condition)
{
#ifdef __SWITCH__
    condvarInit(condition);
    return true;
#else
    return pthread_cond_init(condition, NULL) == 0;
#endif
}

static void runtime_cond_destroy(AirPlayRuntimeCond *condition)
{
#ifndef __SWITCH__
    pthread_cond_destroy(condition);
#else
    (void)condition;
#endif
}

static void runtime_cond_wait(AirPlayRuntimeCond *condition,
                              AirPlayRuntimeMutex *mutex)
{
#ifdef __SWITCH__
    (void)condvarWait(condition, mutex);
#else
    pthread_cond_wait(condition, mutex);
#endif
}

static void runtime_cond_wake_all(AirPlayRuntimeCond *condition)
{
#ifdef __SWITCH__
    (void)condvarWakeAll(condition);
#else
    pthread_cond_broadcast(condition);
#endif
}

static bool runtime_thread_start(AirPlayRuntimeThread *thread,
                                 AirPlayRuntimeThreadEntry entry, void *argument)
{
#ifdef __SWITCH__
    Result result = threadCreate(thread, entry, argument, NULL,
                                 AIRPLAY_RUNTIME_THREAD_STACK_SIZE, 0x2b, -2);
    if (R_FAILED(result))
        return false;
    result = threadStart(thread);
    if (R_FAILED(result))
    {
        threadClose(thread);
        return false;
    }
    return true;
#else
    return pthread_create(thread, NULL, entry, argument) == 0;
#endif
}

static void runtime_thread_join(AirPlayRuntimeThread *thread)
{
#ifdef __SWITCH__
    threadWaitForExit(thread);
    threadClose(thread);
#else
    pthread_join(*thread, NULL);
#endif
}

static bool runtime_enqueue_locked(AirPlayMirrorRuntime *runtime,
                                   AirPlayRuntimeCommand command)
{
    size_t write_index;

    if (runtime->command_count == AIRPLAY_RUNTIME_COMMAND_CAPACITY)
        return false;
    if (command.bridge)
        airplay_stream_bridge_retain(command.bridge);
    write_index = (runtime->command_read + runtime->command_count) %
                  AIRPLAY_RUNTIME_COMMAND_CAPACITY;
    runtime->commands[write_index] = command;
    runtime->command_count++;
    runtime_cond_wake_all(&runtime->command_ready);
    return true;
}

static void runtime_set_status_locked(AirPlayMirrorRuntime *runtime,
                                      AirPlayMirrorRuntimeStatus status)
{
    AirPlayRuntimeCommand command = {
        .type = AIRPLAY_RUNTIME_COMMAND_NOTIFY,
        .generation = runtime->generation,
        .status = status};

    if (runtime->status == status)
        return;
    runtime->status = status;
    (void)runtime_enqueue_locked(runtime, command);
}

static bool runtime_generation_active(AirPlayMirrorRuntime *runtime,
                                      uint32_t generation)
{
    bool active;

    runtime_mutex_lock(&runtime->mutex);
    active = runtime->session_id != 0u && runtime->generation == generation;
    runtime_mutex_unlock(&runtime->mutex);
    return active;
}

static void runtime_command_complete(AirPlayMirrorRuntime *runtime,
                                     uint32_t generation, bool ok,
                                     AirPlayMirrorRuntimeStatus success_status)
{
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->session_id != 0u && runtime->generation == generation)
        runtime_set_status_locked(runtime, ok ? success_status : AIRPLAY_MIRROR_RUNTIME_ERROR);
    runtime_mutex_unlock(&runtime->mutex);
}

static void runtime_process_command(AirPlayMirrorRuntime *runtime,
                                    AirPlayRuntimeCommand *command)
{
    AirPlayMirrorRuntimePlayerOps *player = &runtime->config.player;
    bool bound;
    bool ok = true;

    switch (command->type)
    {
    case AIRPLAY_RUNTIME_COMMAND_LOAD:
        if (!runtime_generation_active(runtime, command->generation))
            break;
        bound = player->bind_stream(command->bridge, command->generation,
                                    player->user_data);
        AIRPLAY_OBSERVE(
            "[airplay-video-pipeline] stage=player-handoff event=bridge-bind "
            "generation=%u result=%s\n",
            command->generation, bound ? "ok" : "failed");
        ok = bound && runtime_generation_active(runtime, command->generation) &&
             player->set_uri(PLAYER_LIBMPV_AIRPLAY_URI,
                             airplay_stream_bridge_profile(command->bridge) ==
                                     AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY
                                 ? "AirPlay Audio"
                                 : "AirPlay Screen Mirroring",
                             command->generation, player->user_data);
        AIRPLAY_OBSERVE(
            "[airplay-video-pipeline] stage=player-handoff event=player-load "
            "generation=%u result=%s\n",
            command->generation, ok ? "ok" : "failed");
        if (!ok)
            (void)player->bind_stream(NULL, command->generation,
                                      player->user_data);
        runtime_command_complete(runtime, command->generation, ok,
                                 AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME);
        break;
    case AIRPLAY_RUNTIME_COMMAND_REPLACE:
        if (!runtime_generation_active(runtime, command->generation))
            break;
        ok = player->stop(command->previous_generation, player->user_data) &&
             player->bind_stream(NULL, command->previous_generation,
                                 player->user_data) &&
             player->replace_generation &&
             player->replace_generation(command->previous_generation,
                                        command->generation,
                                        player->user_data);
        bound = ok && player->bind_stream(command->bridge,
                                          command->generation,
                                          player->user_data);
        ok = bound &&
             runtime_generation_active(runtime, command->generation) &&
             player->set_uri(PLAYER_LIBMPV_AIRPLAY_URI,
                             "AirPlay Screen Mirroring",
                             command->generation, player->user_data);
        AIRPLAY_OBSERVE(
            "[airplay-video-pipeline] stage=player-handoff event=bridge-replace "
            "previous=%u generation=%u result=%s\n",
            command->previous_generation, command->generation,
            ok ? "ok" : "failed");
        if (!ok && bound)
            (void)player->bind_stream(NULL, command->generation,
                                      player->user_data);
        runtime_command_complete(runtime, command->generation, ok,
                                 AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME);
        break;
    case AIRPLAY_RUNTIME_COMMAND_PLAY:
        if (!runtime_generation_active(runtime, command->generation))
            break;
        ok = player->play(command->generation, player->user_data);
        AIRPLAY_OBSERVE(
            "[airplay-video-pipeline] stage=player-handoff event=player-play "
            "generation=%u result=%s\n",
            command->generation, ok ? "ok" : "failed");
        runtime_command_complete(runtime, command->generation, ok,
                                 AIRPLAY_MIRROR_RUNTIME_PLAYING);
        break;
    case AIRPLAY_RUNTIME_COMMAND_STOP:
        if (player->bind_audio)
            (void)player->bind_audio(NULL, command->generation, player->user_data);
        ok = player->stop(command->generation, player->user_data);
        bound = player->bind_stream(NULL, command->generation,
                                    player->user_data);
        AIRPLAY_OBSERVE(
            "[airplay-video-pipeline] stage=player-handoff event=player-stop "
            "generation=%u result=%s unbind=%s\n",
            command->generation, ok ? "ok" : "failed",
            bound ? "ok" : "failed");
        break;
    case AIRPLAY_RUNTIME_COMMAND_AUDIO:
    {
        runtime_mutex_lock(&runtime->mutex);
        bool current = runtime->generation == command->generation &&
                       runtime->session_id != 0u &&
                       (!command->bridge || runtime->audio_bridge == command->bridge);
        runtime_mutex_unlock(&runtime->mutex);
        if (current)
        {
            ok = player->bind_audio && player->bind_audio(
                command->bridge, command->generation, player->user_data);
            AIRPLAY_OBSERVE(
                "[airplay-audio] stage=player-handoff generation=%u action=%s result=%s\n",
                command->generation, command->bridge ? "attach" : "remove",
                ok ? "ok" : "failed");
        }
        break;
    }
    case AIRPLAY_RUNTIME_COMMAND_NOTIFY:
        if (player->status_changed)
            player->status_changed(command->status, command->generation,
                                   player->user_data);
        break;
    }
    airplay_stream_bridge_release(command->bridge);
}

static AIRPLAY_RUNTIME_THREAD_RETURN runtime_worker(void *argument)
{
    AirPlayMirrorRuntime *runtime = argument;

    for (;;)
    {
        AirPlayRuntimeCommand command;

        runtime_mutex_lock(&runtime->mutex);
        while (runtime->command_count == 0u && atomic_load(&runtime->running))
            runtime_cond_wait(&runtime->command_ready, &runtime->mutex);
        if (runtime->command_count == 0u && !atomic_load(&runtime->running))
        {
            runtime_mutex_unlock(&runtime->mutex);
            break;
        }
        command = runtime->commands[runtime->command_read];
        runtime->command_read = (runtime->command_read + 1u) %
                                AIRPLAY_RUNTIME_COMMAND_CAPACITY;
        runtime->command_count--;
        runtime_mutex_unlock(&runtime->mutex);
        runtime_process_command(runtime, &command);
    }
    AIRPLAY_RUNTIME_THREAD_FINISH();
}

static void runtime_video(const AirPlayMirrorAccessUnit *access_unit,
                          void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayStreamBridge *bridge = NULL;
    uint32_t generation = 0u;
    bool recording = false;
    bool pushed;
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    bool emit_diagnostics = false;
    uint64_t now_ms;
#endif

    runtime_mutex_lock(&runtime->mutex);
    bridge = runtime->bridge;
    generation = runtime->generation;
    recording = runtime->recording;
    if (bridge)
        airplay_stream_bridge_retain(bridge);
    runtime_mutex_unlock(&runtime->mutex);
    if (!bridge || !recording)
    {
        airplay_stream_bridge_release(bridge);
        return;
    }
    pushed = airplay_stream_bridge_push_video(bridge, access_unit);
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    now_ms = AIRPLAY_TRACE_NOW_MS();
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->generation == generation &&
        (runtime->diagnostic_last_video_ms == 0u ||
         now_ms - runtime->diagnostic_last_video_ms >= 1000u))
    {
        runtime->diagnostic_last_video_ms = now_ms;
        emit_diagnostics = true;
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (emit_diagnostics)
    {
        AirPlayStreamBridgeStats stats;

        if (airplay_stream_bridge_get_stats(bridge, &stats))
        {
            AIRPLAY_OBSERVE(
                "[airplay-video-pipeline] stage=bridge generation=%u "
                "push=%s video=%llu/%llu/%llu audio=%llu/%llu/%llu "
                "buffer=%zu/%zu written=%llu read=%llu config=%u\n",
                generation, pushed ? "ok" : "failed",
                (unsigned long long)stats.video_packets,
                (unsigned long long)stats.video_bytes,
                (unsigned long long)stats.video_push_failures,
                (unsigned long long)stats.audio_packets,
                (unsigned long long)stats.audio_bytes,
                (unsigned long long)stats.audio_push_failures,
                stats.buffered, stats.capacity,
                (unsigned long long)stats.bytes_written,
                (unsigned long long)stats.bytes_read,
                stats.video_config_generation);
        }
    }
#endif
    airplay_stream_bridge_release(bridge);

    runtime_mutex_lock(&runtime->mutex);
    if (runtime->generation == generation && runtime->recording)
    {
        if (!pushed)
            runtime_set_status_locked(runtime, AIRPLAY_MIRROR_RUNTIME_ERROR);
        else if (access_unit->keyframe && !runtime->player_play_queued)
        {
            AirPlayRuntimeCommand command = {
                .type = AIRPLAY_RUNTIME_COMMAND_PLAY,
                .generation = generation};
            if (runtime_enqueue_locked(runtime, command))
                runtime->player_play_queued = true;
            else
                runtime_set_status_locked(runtime, AIRPLAY_MIRROR_RUNTIME_ERROR);
        }
    }
    runtime_mutex_unlock(&runtime->mutex);
}

static void runtime_audio(const AirPlayMirrorAudioFrame *frame, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayStreamBridge *bridge = NULL;
    AirPlayStreamBridge *video_bridge = NULL;
    uint32_t generation;
    AirPlayStreamBridgeStats stats = {0};
    bool mirror_active;
    bool pushed;
    bool report_failure = false;
    bool report_progress = false;

    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening || !runtime->audio_format_ready)
    {
        runtime_mutex_unlock(&runtime->mutex);
        return;
    }
    mirror_active = runtime->mirror != NULL;
    if (mirror_active)
    {
        video_bridge = runtime->bridge;
        airplay_stream_bridge_retain(video_bridge);
    }
    bridge = mirror_active ? runtime->audio_bridge : runtime->bridge;
    generation = runtime->generation;
    if (bridge && runtime->recording)
        airplay_stream_bridge_retain(bridge);
    else
        bridge = NULL;
    runtime_mutex_unlock(&runtime->mutex);
    if (bridge && video_bridge)
        (void)airplay_stream_bridge_sync_video_timeline(bridge, video_bridge);
    airplay_stream_bridge_release(video_bridge);
    if (!bridge)
        return;
    if (!mirror_active)
    {
        bool report_sink = false;

        runtime_mutex_lock(&runtime->mutex);
        if (runtime->generation == generation && !runtime->audio_sink_reported)
        {
            runtime->audio_sink_reported = true;
            report_sink = true;
        }
        runtime_mutex_unlock(&runtime->mutex);
        if (report_sink)
            AIRPLAY_TRACE(
                "[airplay-audio] stage=unsupported-playback-sink "
                "generation=%u sequence=%u bytes=%zu\n",
                generation, frame->sequence, frame->size);
        airplay_stream_bridge_release(bridge);
        return;
    }
    pushed = airplay_stream_bridge_push_audio(bridge, frame);
    (void)airplay_stream_bridge_get_stats(bridge, &stats);
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->generation == generation && runtime->recording &&
        runtime->audio_bridge == bridge)
    {
        if (!pushed && !runtime->audio_mux_failure_reported)
        {
            runtime->audio_mux_failure_reported = true;
            report_failure = true;
        }
        else if (pushed && runtime->player_load_queued && stats.audio_packets != 0u &&
                 !runtime->audio_attach_queued)
        {
            AirPlayRuntimeCommand command = {
                .type = AIRPLAY_RUNTIME_COMMAND_AUDIO,
                .generation = generation, .bridge = bridge};

            if (runtime_enqueue_locked(runtime, command))
                runtime->audio_attach_queued = true;
        }
        runtime->diagnostic_audio_frames++;
        if (runtime->diagnostic_audio_frames == 1u ||
            (runtime->diagnostic_audio_frames & 0xffu) == 0u ||
            (stats.audio_packets == 1u && runtime->diagnostic_audio_packets == 0u))
        {
            runtime->diagnostic_audio_packets = stats.audio_packets;
            report_progress = true;
        }
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (report_failure)
        AIRPLAY_TRACE_WARN("[airplay-audio] stage=mux-failed generation=%u packets=%llu failures=%llu\n",
                           generation, (unsigned long long)stats.audio_packets,
                           (unsigned long long)stats.audio_push_failures);
    if (report_progress)
        AIRPLAY_OBSERVE("[airplay-audio] stage=bridge generation=%u packets=%llu bytes=%llu failures=%llu buffered=%zu written=%llu read=%llu "
                        "clock_ready=%u/%u drops=%llu/%llu candidate_pts=%lld video_pts=%lld video_anchor=%016llx sync_ntp=%016llx sync_rtp=%u\n",
                        generation, (unsigned long long)stats.audio_packets,
                        (unsigned long long)stats.audio_bytes,
                        (unsigned long long)stats.audio_push_failures, stats.buffered,
                        (unsigned long long)stats.bytes_written,
                        (unsigned long long)stats.bytes_read,
                        stats.clock.video_ready ? 1u : 0u,
                        stats.clock.audio_sync_ready ? 1u : 0u,
                        (unsigned long long)stats.clock.unsynced_audio_drops,
                        (unsigned long long)stats.clock.skew_audio_drops,
                        (long long)stats.clock.candidate_audio_pts,
                        (long long)stats.clock.last_video_pts,
                        (unsigned long long)stats.clock.video_anchor_ntp,
                        (unsigned long long)stats.clock.audio_sync_ntp,
                        stats.clock.audio_sync_rtp);
    airplay_stream_bridge_release(bridge);
}

static void runtime_audio_sync(uint32_t rtp_timestamp, uint64_t ntp_timestamp,
                               void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayStreamBridge *bridge = NULL;
    bool uses_ntp_timing;
    bool report_sync = false;

    runtime_mutex_lock(&runtime->mutex);
    uses_ntp_timing = runtime->uses_ntp_timing;
    if (!runtime->opening && runtime->audio_format_ready)
    {
        bridge = runtime->mirror ? runtime->audio_bridge : runtime->bridge;
        if (bridge)
        {
            airplay_stream_bridge_retain(bridge);
            report_sync = !runtime->audio_sync_reported;
            runtime->audio_sync_reported = true;
        }
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (!bridge)
        return;
    // Mirror payload timestamps omit the NTP 1900-to-1970 epoch offset.
    // Modular Q32.32 subtraction also preserves fractions across NTP era wrap.
    uint64_t normalized_ntp = uses_ntp_timing
        ? ntp_timestamp - (UINT64_C(2208988800) << 32) : ntp_timestamp;
    if (report_sync)
        AIRPLAY_OBSERVE("[airplay-audio] stage=sync-clock protocol=%s rtp=%u raw_ntp=%016llx normalized_ntp=%016llx\n",
                        uses_ntp_timing ? "NTP" : "none", rtp_timestamp,
                        (unsigned long long)ntp_timestamp,
                        (unsigned long long)normalized_ntp);
    if (!airplay_stream_bridge_update_audio_sync(bridge, rtp_timestamp,
                                                 normalized_ntp))
        AIRPLAY_TRACE_WARN("[airplay-clock] rejected audio sync metadata\n");
    airplay_stream_bridge_release(bridge);
}

bool airplay_mirror_runtime_create(const AirPlayMirrorRuntimeConfig *config,
                                   AirPlayMirrorRuntime **runtime_out)
{
    AirPlayMirrorRuntime *runtime;
    bool mutex_ready = false;
    bool condition_ready = false;

    if (!config || !runtime_out || *runtime_out || !config->player.bind_stream ||
        !config->player.set_uri || !config->player.play || !config->player.stop)
        return false;
    runtime = calloc(1, sizeof(*runtime));
    if (!runtime)
        return false;
    runtime->config = *config;
    runtime->status = AIRPLAY_MIRROR_RUNTIME_IDLE;
    mutex_ready = runtime_mutex_init(&runtime->mutex);
    condition_ready = mutex_ready && runtime_cond_init(&runtime->command_ready);
    atomic_init(&runtime->running, condition_ready);
    if (!condition_ready ||
        !runtime_thread_start(&runtime->thread, runtime_worker, runtime))
    {
        if (condition_ready)
            airplay_diagnostics_thread_create_failed(
                RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME);
        atomic_store(&runtime->running, false);
        if (condition_ready)
            runtime_cond_destroy(&runtime->command_ready);
        if (mutex_ready)
            runtime_mutex_destroy(&runtime->mutex);
        free(runtime);
        return false;
    }
    runtime->thread_started = true;
    runtime->diagnostic_thread_generation =
        airplay_diagnostics_thread_created(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME);
    *runtime_out = runtime;
    return true;
}

void airplay_mirror_runtime_destroy(AirPlayMirrorRuntime *runtime)
{
    if (!runtime)
        return;
    airplay_mirror_runtime_stop(0u, runtime);
    atomic_store(&runtime->running, false);
    runtime_mutex_lock(&runtime->mutex);
    runtime_cond_wake_all(&runtime->command_ready);
    runtime_mutex_unlock(&runtime->mutex);
    if (runtime->thread_started)
    {
        runtime_thread_join(&runtime->thread);
        airplay_diagnostics_thread_joined(
            RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME,
            runtime->diagnostic_thread_generation);
    }
    runtime_cond_destroy(&runtime->command_ready);
    runtime_mutex_destroy(&runtime->mutex);
    memset(runtime, 0, sizeof(*runtime));
    free(runtime);
}

bool airplay_mirror_runtime_transport_prepare(uint64_t session_id,
                                              const uint8_t key[16],
                                              const uint8_t iv[16],
                                              uint32_t peer_ipv4_address,
                                              uint16_t peer_timing_port,
                                              bool uses_ntp_timing,
                                              uint16_t *timing_port_out,
                                              void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayMirrorTiming *timing = NULL;
    bool accepted = false;

    (void)key;
    (void)iv;
    if (!runtime || !timing_port_out || session_id == 0u ||
        (uses_ntp_timing &&
         (peer_ipv4_address == 0u || peer_timing_port == 0u)))
        return false;
    *timing_port_out = 0u;
    if (uses_ntp_timing &&
        !airplay_mirror_timing_create(peer_ipv4_address, peer_timing_port,
                                      &timing))
        return false;

    runtime_mutex_lock(&runtime->mutex);
    if (runtime->transport_session_id == 0u && runtime->session_id == 0u &&
        !runtime->timing)
    {
        runtime->transport_session_id = session_id;
        runtime->timing = timing;
        runtime->uses_ntp_timing = uses_ntp_timing;
        accepted = true;
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (!accepted)
    {
        airplay_mirror_timing_destroy(timing);
        return false;
    }
    if (timing)
        *timing_port_out = airplay_mirror_timing_port(timing);
    AIRPLAY_TRACE("[airplay-timing] session=%llu protocol=%s local=%u remote=%u\n",
                  (unsigned long long)session_id,
                  uses_ntp_timing ? "NTP" : "none", *timing_port_out,
                  peer_timing_port);
    return !uses_ntp_timing || *timing_port_out != 0u;
}

bool airplay_mirror_runtime_open(uint64_t session_id, const uint8_t key[16],
                                 uint64_t stream_connection_id,
                                 uint16_t *data_port_out, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayMirrorSessionConfig mirror_config = {0};
    AirPlayMirrorSession *mirror = NULL;
    AirPlayStreamBridge *bridge = NULL;
    AirPlayStreamBridge *previous_bridge = NULL;
    AirPlayStreamBridge *audio_bridge = NULL;
    AirPlayMirrorAudioFormat audio_format = {0};
    AirPlayRuntimeCommand command = {0};
    uint32_t previous_generation;
    uint32_t generation;
    bool promotion = false;
    bool recording = false;
    bool player_load_queued = false;
    bool accepted = false;
    bool has_audio = false;
    const char *failure_stage = "bridge-config";

    if (!runtime || !key || !data_port_out || session_id == 0u ||
        stream_connection_id == 0u)
        return false;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening || runtime->mirror ||
        (runtime->transport_session_id != 0u &&
         runtime->transport_session_id != session_id) ||
        (runtime->session_id != 0u &&
         (runtime->session_id != session_id || !runtime->bridge ||
          airplay_stream_bridge_profile(runtime->bridge) !=
              AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY)))
    {
        runtime_mutex_unlock(&runtime->mutex);
        return false;
    }
    promotion = runtime->session_id == session_id;
    has_audio = runtime->audio && runtime->audio_format_ready;
    audio_format = runtime->audio_format;
    runtime->opening = true;
    previous_generation = runtime->generation;
    generation = previous_generation + 1u;
    if (generation == 0u)
        generation = 1u;
    runtime_mutex_unlock(&runtime->mutex);

    mirror_config.session_id = session_id;
    mirror_config.session_key = key;
    mirror_config.stream_connection_id = stream_connection_id;
    mirror_config.video_callback = runtime_video;
    mirror_config.video_user_data = runtime;
    if (!airplay_stream_bridge_create_profile(
            runtime->config.stream_capacity,
            AIRPLAY_STREAM_BRIDGE_PROFILE_VIDEO_AUDIO, &bridge))
        goto cleanup;
    if (has_audio)
    {
        // A previous video's demuxer may have cancelled the old audio reader.
        if (!airplay_stream_bridge_create_profile(runtime->config.stream_capacity,
                AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY, &audio_bridge) ||
            !airplay_stream_bridge_configure_audio(audio_bridge, &audio_format))
            goto cleanup;
        (void)airplay_stream_bridge_sync_video_timeline(audio_bridge, bridge);
    }
    failure_stage = "socket-data";
    if (!airplay_mirror_session_create(&mirror_config, &mirror))
        goto cleanup;
    failure_stage = "runtime-state";

    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening && runtime->generation == previous_generation &&
        !runtime->mirror &&
        (runtime->transport_session_id == 0u ||
         runtime->transport_session_id == session_id) &&
        ((!promotion && runtime->session_id == 0u) ||
         (promotion && runtime->session_id == session_id &&
          runtime->bridge &&
          airplay_stream_bridge_profile(runtime->bridge) ==
              AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY)) &&
        (!runtime->recording ||
         runtime->command_count < AIRPLAY_RUNTIME_COMMAND_CAPACITY))
    {
        previous_bridge = runtime->bridge;
        recording = runtime->recording;
        player_load_queued = runtime->player_load_queued;
        runtime->mirror = mirror;
        runtime->bridge = bridge;
        runtime->audio_bridge = audio_bridge;
        audio_bridge = NULL;
        runtime->audio_attach_queued = false;
        runtime->audio_mux_failure_reported = false;
        runtime->diagnostic_audio_packets = 0u;
        runtime->diagnostic_audio_frames = 0u;
        runtime->audio_sync_reported = false;
        runtime->transport_session_id = session_id;
        runtime->session_id = session_id;
        runtime->generation = generation;
        runtime->player_load_queued = false;
        runtime->player_play_queued = false;
        runtime->audio_sink_reported = false;
        runtime->diagnostic_last_video_ms = 0u;
        runtime->opening = false;
        if (recording && player_load_queued)
        {
            command = (AirPlayRuntimeCommand){
                .type = AIRPLAY_RUNTIME_COMMAND_REPLACE,
                .generation = generation,
                .bridge = bridge,
                .previous_generation = previous_generation};
            /* Capacity is reserved by the state predicate under this lock. */
            (void)runtime_enqueue_locked(runtime, command);
            runtime->player_load_queued = true;
            runtime_set_status_locked(
                runtime, AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME);
        }
        else
            runtime_set_status_locked(runtime,
                                      AIRPLAY_MIRROR_RUNTIME_PREPARING);
        accepted = true;
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (accepted)
    {
        if (recording)
            airplay_mirror_session_set_recording(mirror, true);
        if (previous_bridge)
            airplay_stream_bridge_cancel(previous_bridge);
        airplay_stream_bridge_release(previous_bridge);
        *data_port_out = airplay_mirror_session_port(mirror);
        AIRPLAY_TRACE(
            "[airplay-media] session=%llu generation=%u open port=%u "
            "promotion=%u recording=%u\n",
            (unsigned long long)session_id, generation, *data_port_out,
            promotion ? 1u : 0u, recording ? 1u : 0u);
        return *data_port_out != 0u;
    }

cleanup:
    (void)failure_stage;
    AIRPLAY_OBSERVE(
        "[airplay-setup-failure] session=%llu stream=video stage=%s generation=%u\n",
        (unsigned long long)session_id, failure_stage, generation);
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening && runtime->generation == previous_generation)
    {
        runtime->opening = false;
        if (!promotion)
            runtime_set_status_locked(runtime,
                                      AIRPLAY_MIRROR_RUNTIME_ERROR);
    }
    runtime_mutex_unlock(&runtime->mutex);
    airplay_mirror_session_destroy(mirror);
    airplay_stream_bridge_release(audio_bridge);
    airplay_stream_bridge_release(bridge);
    return false;

}

bool airplay_mirror_runtime_audio_open(
    uint64_t session_id, const uint8_t key[16], const uint8_t iv[16],
    uint8_t compression_type, uint16_t samples_per_frame,
    uint32_t sample_rate, uint16_t *data_port_out,
    uint16_t *control_port_out, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayMirrorAudioConfig audio_config = {0};
    AirPlayMirrorAudioFormat format;
    AirPlayMirrorAudio *audio = NULL;
    AirPlayStreamBridge *bridge = NULL;
    AirPlayStreamBridge *replacement = NULL;
    uint32_t previous_generation;
    uint32_t generation;
    bool bridge_created = false;
    bool deferred = false;
    bool accepted = false;
    bool recording = false;
    const char *failure_stage = "audio-create";

    if (!runtime || !key || !iv || !data_port_out || !control_port_out ||
        session_id == 0u)
    {
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=audio stage=config ct=%u spf=%u sr=%u\n",
            (unsigned long long)session_id, compression_type,
            samples_per_frame, sample_rate);
        return false;
    }
    if (!airplay_mirror_audio_format(compression_type, samples_per_frame,
                                     sample_rate, &format))
    {
        AIRPLAY_TRACE_SYNC(
            "[airplay-setup-failure] session=%llu stream=audio stage=format ct=%u spf=%u sr=%u\n",
            (unsigned long long)session_id, compression_type,
            samples_per_frame, sample_rate);
        return false;
    }
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening || runtime->audio ||
        runtime->transport_session_id != session_id ||
        (runtime->session_id != 0u && runtime->session_id != session_id))
    {
        bool opening = runtime->opening;
        bool has_audio = runtime->audio != NULL;
        uint64_t transport_session_id = runtime->transport_session_id;
        uint64_t mirror_session_id = runtime->session_id;

        runtime_mutex_unlock(&runtime->mutex);
        (void)opening;
        (void)has_audio;
        (void)transport_session_id;
        (void)mirror_session_id;
        AIRPLAY_TRACE_SYNC(
            "[airplay-setup-failure] session=%llu stream=audio stage=runtime-state "
            "opening=%u has_audio=%u transport_session=%llu mirror_session=%llu\n",
            (unsigned long long)session_id, opening ? 1u : 0u,
            has_audio ? 1u : 0u,
            (unsigned long long)transport_session_id,
            (unsigned long long)mirror_session_id);
        return false;
    }
    runtime->opening = true;
    previous_generation = runtime->generation;
    generation = previous_generation;
    bridge = runtime->bridge;
    bridge_created = bridge == NULL;
    if (bridge_created)
    {
        generation++;
        if (generation == 0u)
            generation = 1u;
    }
    else
        airplay_stream_bridge_retain(bridge);
    runtime_mutex_unlock(&runtime->mutex);
    if (bridge_created &&
        !airplay_stream_bridge_create_profile(
            runtime->config.stream_capacity,
            AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY, &bridge))
    {
        failure_stage = "bridge-create";
        goto cleanup;
    }
    audio_config.session_id = session_id;
    audio_config.aes_key = key;
    audio_config.aes_iv = iv;
    audio_config.compression_type = compression_type;
    audio_config.samples_per_frame = format.samples_per_frame;
    audio_config.sample_rate = format.sample_rate;
    audio_config.callback = runtime_audio;
    audio_config.sync_callback = runtime_audio_sync;
    audio_config.callback_user_data = runtime;
    if (!airplay_mirror_audio_create(&audio_config, &audio))
        goto cleanup;
    failure_stage = "bridge-config";
    if (bridge_created)
    {
        if (!airplay_stream_bridge_configure_audio(bridge, &format))
            goto cleanup;
    }
    else
    {
        if (!airplay_stream_bridge_create_profile(runtime->config.stream_capacity,
                AIRPLAY_STREAM_BRIDGE_PROFILE_AUDIO_ONLY, &replacement) ||
            !airplay_stream_bridge_configure_audio(replacement, &format))
            goto cleanup;
        (void)airplay_stream_bridge_sync_video_timeline(replacement, bridge);
    }
    deferred = !bridge_created;
    failure_stage = "runtime-state";
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening &&
        runtime->transport_session_id == session_id &&
        (runtime->session_id == 0u || runtime->session_id == session_id) &&
        runtime->generation == previous_generation && !runtime->audio &&
        ((bridge_created && !runtime->bridge && runtime->session_id == 0u) ||
         (!bridge_created && runtime->bridge == bridge)))
    {
        runtime->audio = audio;
        runtime->audio_format = format;
        runtime->audio_format_ready = true;
        runtime->audio_bridge = replacement;
        replacement = NULL;
        runtime->audio_attach_queued = false;
        runtime->audio_mux_failure_reported = false;
        runtime->diagnostic_audio_packets = 0u;
        runtime->diagnostic_audio_frames = 0u;
        runtime->audio_sync_reported = false;
        if (bridge_created)
        {
            runtime->bridge = bridge;
            runtime->session_id = session_id;
            runtime->generation = generation;
            runtime->player_load_queued = false;
            runtime->player_play_queued = false;
            runtime->audio_sink_reported = false;
            runtime_set_status_locked(runtime,
                                      AIRPLAY_MIRROR_RUNTIME_PREPARING);
        }
        runtime->opening = false;
        recording = runtime->recording;
        accepted = true;
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (accepted)
    {
        if (recording)
            airplay_mirror_audio_set_recording(audio, true);
        *data_port_out = airplay_mirror_audio_data_port(audio);
        *control_port_out = airplay_mirror_audio_control_port(audio);
        AIRPLAY_TRACE_SYNC(
            "[airplay-audio] session=%llu stage=open ct=%u spf=%u sr=%u "
            "data=%u control=%u profile=%s generation=%u\n",
            (unsigned long long)session_id, compression_type,
            format.samples_per_frame, format.sample_rate, *data_port_out,
            *control_port_out,
            bridge_created ? "audio-only" : "video-audio", generation);
        AIRPLAY_OBSERVE(
            "[airplay-audio] session=%llu stage=track-ready generation=%u transition=%s\n",
            (unsigned long long)session_id, generation,
            deferred ? "waiting-audio" : "in-place");
        (void)deferred;
        if (!bridge_created)
            airplay_stream_bridge_release(bridge);
        return *data_port_out != 0u && *control_port_out != 0u;
    }

cleanup:
    (void)failure_stage;
    AIRPLAY_TRACE_SYNC(
        "[airplay-setup-failure] session=%llu stream=audio stage=%s generation=%u ct=%u spf=%u sr=%u\n",
        (unsigned long long)session_id, failure_stage, generation,
        compression_type, samples_per_frame, sample_rate);
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->opening && runtime->generation == previous_generation &&
        runtime->transport_session_id == session_id)
        runtime->opening = false;
    runtime_mutex_unlock(&runtime->mutex);
    airplay_mirror_audio_destroy(audio);
    airplay_stream_bridge_release(replacement);
    airplay_stream_bridge_release(bridge);
    return false;
}

bool airplay_mirror_runtime_record_audio(uint64_t session_id,
                                         void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;

    if (!runtime)
        return false;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->session_id != session_id || !runtime->bridge ||
        !runtime->audio || (runtime->mirror && !runtime->recording) ||
        (!runtime->mirror && runtime->recording))
    {
        runtime_mutex_unlock(&runtime->mutex);
        AIRPLAY_OBSERVE(
            "[airplay-setup-failure] session=%llu stream=audio "
            "stage=record state-mismatch\n",
            (unsigned long long)session_id);
        return false;
    }
    bool mirror_active = runtime->mirror != NULL;

    if (!mirror_active)
    {
        runtime->recording = true;
        runtime->player_load_queued = false;
        runtime->player_play_queued = false;
    }
    airplay_mirror_audio_set_recording(runtime->audio, true);
    runtime_mutex_unlock(&runtime->mutex);
    AIRPLAY_TRACE(
        "[airplay-audio] session=%llu stage=record profile=%s\n",
        (unsigned long long)session_id,
        mirror_active ? "video-audio" : "audio-only");
    return true;
}

void airplay_mirror_runtime_record(uint64_t session_id, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayRuntimeCommand command;

    if (!runtime)
        return;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->session_id != session_id || !runtime->bridge ||
        !runtime->mirror || runtime->player_load_queued)
    {
        runtime_mutex_unlock(&runtime->mutex);
        return;
    }
    runtime->recording = true;
    runtime->player_load_queued = true;
    if (runtime->mirror)
        airplay_mirror_session_set_recording(runtime->mirror, true);
    if (runtime->audio)
        airplay_mirror_audio_set_recording(runtime->audio, true);
    command = (AirPlayRuntimeCommand){
        .type = AIRPLAY_RUNTIME_COMMAND_LOAD,
        .generation = runtime->generation,
        .bridge = runtime->bridge};
    if (!runtime_enqueue_locked(runtime, command))
    {
        runtime->player_load_queued = false;
        runtime_set_status_locked(runtime, AIRPLAY_MIRROR_RUNTIME_ERROR);
    }
    else
        runtime_set_status_locked(runtime, AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME);
    runtime_mutex_unlock(&runtime->mutex);
}

bool airplay_mirror_runtime_stop_streams(uint64_t session_id, bool video,
                                        bool audio, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayMirrorSession *removed_mirror = NULL;
    AirPlayMirrorAudio *removed_audio = NULL;
    AirPlayStreamBridge *removed_bridge = NULL;
    AirPlayStreamBridge *removed_audio_bridge = NULL;
    uint32_t generation;
    bool keep_video, keep_audio, had_player, had_video;

    if (!runtime || session_id == 0u || (!video && !audio))
        return false;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->transport_session_id != session_id || runtime->opening ||
        runtime->command_count > AIRPLAY_RUNTIME_COMMAND_CAPACITY - 2u)
    {
        runtime_mutex_unlock(&runtime->mutex);
        return false;
    }
    runtime->opening = true;
    generation = runtime->generation;
    had_video = runtime->mirror != NULL;
    keep_video = runtime->mirror && !video;
    keep_audio = runtime->audio && !audio;
    if (video)
    {
        removed_mirror = runtime->mirror;
        runtime->mirror = NULL;
    }
    if (audio)
    {
        removed_audio = runtime->audio;
        runtime->audio = NULL;
        runtime->audio_format_ready = false;
        memset(&runtime->audio_format, 0, sizeof(runtime->audio_format));
        removed_audio_bridge = runtime->audio_bridge;
        runtime->audio_bridge = NULL;
    }
    if (keep_video && audio && runtime->audio_attach_queued)
    {
        AirPlayRuntimeCommand command = {
            .type = AIRPLAY_RUNTIME_COMMAND_AUDIO, .generation = generation};
        (void)runtime_enqueue_locked(runtime, command);
    }
    if (audio || !keep_video)
        runtime->audio_attach_queued = false;
    if (!keep_video)
    {
        if (had_video || !keep_audio)
        {
            removed_bridge = runtime->bridge;
            runtime->bridge = had_video && keep_audio ? runtime->audio_bridge : NULL;
        }
        runtime->audio_bridge = NULL;
        runtime->session_id = keep_audio ? session_id : 0u;
        runtime->recording = keep_audio && runtime->recording;
        had_player = runtime->player_load_queued;
        runtime->player_load_queued = false;
        runtime->player_play_queued = false;
        runtime->audio_sink_reported = false;
        if (had_player)
        {
            AirPlayRuntimeCommand command = {
                .type = AIRPLAY_RUNTIME_COMMAND_STOP,
                .generation = generation};
            (void)runtime_enqueue_locked(runtime, command);
            runtime_set_status_locked(runtime, AIRPLAY_MIRROR_RUNTIME_DISCONNECTED);
        }
        runtime->generation++;
        if (runtime->generation == 0u)
            runtime->generation = 1u;
    }
    runtime_mutex_unlock(&runtime->mutex);
    if (removed_bridge)
        airplay_stream_bridge_cancel(removed_bridge);
    if (removed_audio_bridge)
        airplay_stream_bridge_cancel(removed_audio_bridge);
    airplay_mirror_audio_destroy(removed_audio);
    airplay_mirror_session_destroy(removed_mirror);
    airplay_stream_bridge_release(removed_bridge);
    airplay_stream_bridge_release(removed_audio_bridge);
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->transport_session_id == session_id)
        runtime->opening = false;
    runtime_mutex_unlock(&runtime->mutex);
    AIRPLAY_OBSERVE(
        "[airplay-streams] session=%llu remove_video=%u remove_audio=%u remaining_video=%u remaining_audio=%u result=%s\n",
        (unsigned long long)session_id, video ? 1u : 0u, audio ? 1u : 0u,
        keep_video ? 1u : 0u, keep_audio ? 1u : 0u, "ok");
    return true;
}

void airplay_mirror_runtime_stop(uint64_t session_id, void *user_data)
{
    AirPlayMirrorRuntime *runtime = user_data;
    AirPlayMirrorSession *mirror;
    AirPlayMirrorAudio *audio;
    AirPlayMirrorTiming *timing;
    AirPlayStreamBridge *bridge;
    AirPlayStreamBridge *audio_bridge;
    AirPlayRuntimeCommand command;
    bool had_player;
    bool had_session;

    if (!runtime)
        return;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->session_id == 0u && runtime->transport_session_id == 0u)
    {
        runtime_mutex_unlock(&runtime->mutex);
        return;
    }
    if (session_id != 0u && runtime->session_id != session_id &&
        runtime->transport_session_id != session_id)
    {
        runtime_mutex_unlock(&runtime->mutex);
        return;
    }
    had_player = runtime->player_load_queued;
    had_session = runtime->session_id != 0u;
    mirror = runtime->mirror;
    audio = runtime->audio;
    timing = runtime->timing;
    bridge = runtime->bridge;
    audio_bridge = runtime->audio_bridge;
    runtime->mirror = NULL;
    runtime->audio = NULL;
    memset(&runtime->audio_format, 0, sizeof(runtime->audio_format));
    runtime->audio_format_ready = false;
    runtime->timing = NULL;
    runtime->uses_ntp_timing = false;
    runtime->bridge = NULL;
    runtime->audio_bridge = NULL;
    runtime->audio_attach_queued = false;
    runtime->transport_session_id = 0u;
    runtime->session_id = 0u;
    runtime->opening = false;
    runtime->recording = false;
    runtime->player_load_queued = false;
    runtime->player_play_queued = false;
    runtime->audio_sink_reported = false;
    runtime->diagnostic_last_video_ms = 0u;
    if (had_player)
    {
        command = (AirPlayRuntimeCommand){
            .type = AIRPLAY_RUNTIME_COMMAND_STOP,
            .generation = runtime->generation};
        (void)runtime_enqueue_locked(runtime, command);
    }
    if (had_session)
        runtime_set_status_locked(runtime,
                                  AIRPLAY_MIRROR_RUNTIME_DISCONNECTED);
    runtime_mutex_unlock(&runtime->mutex);

    airplay_mirror_timing_destroy(timing);
    if (bridge)
        airplay_stream_bridge_cancel(bridge);
    if (audio_bridge)
        airplay_stream_bridge_cancel(audio_bridge);
    airplay_mirror_audio_destroy(audio);
    airplay_mirror_session_destroy(mirror);
    airplay_stream_bridge_release(bridge);
    airplay_stream_bridge_release(audio_bridge);
}

AirPlayMirrorRuntimeStatus airplay_mirror_runtime_status(
    AirPlayMirrorRuntime *runtime, uint32_t *generation_out)
{
    AirPlayMirrorRuntimeStatus status = AIRPLAY_MIRROR_RUNTIME_ERROR;

    if (!runtime)
        return status;
    runtime_mutex_lock(&runtime->mutex);
    status = runtime->status;
    if (generation_out)
        *generation_out = runtime->generation;
    runtime_mutex_unlock(&runtime->mutex);
    return status;
}

AirPlayStreamBridgeProfile airplay_mirror_runtime_profile(
    AirPlayMirrorRuntime *runtime)
{
    AirPlayStreamBridgeProfile profile =
        AIRPLAY_STREAM_BRIDGE_PROFILE_VIDEO_AUDIO;

    if (!runtime)
        return profile;
    runtime_mutex_lock(&runtime->mutex);
    if (runtime->bridge)
        profile = airplay_stream_bridge_profile(runtime->bridge);
    runtime_mutex_unlock(&runtime->mutex);
    return profile;
}

const char *airplay_mirror_runtime_status_name(AirPlayMirrorRuntimeStatus status)
{
    switch (status)
    {
    case AIRPLAY_MIRROR_RUNTIME_IDLE:
        return "idle";
    case AIRPLAY_MIRROR_RUNTIME_PREPARING:
        return "preparing";
    case AIRPLAY_MIRROR_RUNTIME_WAITING_KEYFRAME:
        return "waiting-keyframe";
    case AIRPLAY_MIRROR_RUNTIME_PLAYING:
        return "playing";
    case AIRPLAY_MIRROR_RUNTIME_DISCONNECTED:
        return "disconnected";
    case AIRPLAY_MIRROR_RUNTIME_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
