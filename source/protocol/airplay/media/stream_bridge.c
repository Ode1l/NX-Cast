#include "stream_bridge.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex AirPlayBridgeMutex;
typedef CondVar AirPlayBridgeCond;
#else
#include <pthread.h>
typedef pthread_mutex_t AirPlayBridgeMutex;
typedef pthread_cond_t AirPlayBridgeCond;
#endif

#define AIRPLAY_STREAM_BRIDGE_AVIO_SIZE 4096u

struct AirPlayStreamBridge
{
    atomic_uint references;
    AirPlayBridgeMutex ring_mutex;
    AirPlayBridgeMutex mux_mutex;
    AirPlayBridgeCond readable;
    AirPlayBridgeCond writable;
    uint8_t *ring;
    size_t capacity;
    size_t read_offset;
    size_t write_offset;
    size_t buffered;
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t video_packets;
    uint64_t audio_packets;
    uint32_t video_config_generation;
    bool reader_claimed;
    bool eof;
    atomic_bool cancelled;
    bool mux_finished;
    bool header_written;
    AVFormatContext *format;
    AVIOContext *avio;
    int stream_index;
    int audio_stream_index;
    int64_t last_pts;
    int64_t last_audio_pts;
};

static bool bridge_mutex_init(AirPlayBridgeMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void bridge_mutex_destroy(AirPlayBridgeMutex *mutex)
{
#ifndef __SWITCH__
    pthread_mutex_destroy(mutex);
#else
    (void)mutex;
#endif
}

static void bridge_mutex_lock(AirPlayBridgeMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void bridge_mutex_unlock(AirPlayBridgeMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

static bool bridge_cond_init(AirPlayBridgeCond *condition)
{
#ifdef __SWITCH__
    condvarInit(condition);
    return true;
#else
    return pthread_cond_init(condition, NULL) == 0;
#endif
}

static void bridge_cond_destroy(AirPlayBridgeCond *condition)
{
#ifndef __SWITCH__
    pthread_cond_destroy(condition);
#else
    (void)condition;
#endif
}

static void bridge_cond_wait(AirPlayBridgeCond *condition,
                             AirPlayBridgeMutex *mutex)
{
#ifdef __SWITCH__
    (void)condvarWait(condition, mutex);
#else
    pthread_cond_wait(condition, mutex);
#endif
}

static void bridge_cond_wake_all(AirPlayBridgeCond *condition)
{
#ifdef __SWITCH__
    (void)condvarWakeAll(condition);
#else
    pthread_cond_broadcast(condition);
#endif
}

static int bridge_avio_write(void *opaque, const uint8_t *input, int input_size)
{
    AirPlayStreamBridge *bridge = opaque;
    size_t first;

    if (!bridge || !input || input_size <= 0 || (size_t)input_size > bridge->capacity)
        return AVERROR(EINVAL);
    bridge_mutex_lock(&bridge->ring_mutex);
    while (!atomic_load(&bridge->cancelled) &&
           bridge->capacity - bridge->buffered < (size_t)input_size)
        bridge_cond_wait(&bridge->writable, &bridge->ring_mutex);
    if (atomic_load(&bridge->cancelled))
    {
        bridge_mutex_unlock(&bridge->ring_mutex);
        return AVERROR_EXIT;
    }
    first = bridge->capacity - bridge->write_offset;
    if (first > (size_t)input_size)
        first = (size_t)input_size;
    memcpy(bridge->ring + bridge->write_offset, input, first);
    memcpy(bridge->ring, input + first, (size_t)input_size - first);
    bridge->write_offset = (bridge->write_offset + (size_t)input_size) % bridge->capacity;
    bridge->buffered += (size_t)input_size;
    bridge->bytes_written += (uint64_t)input_size;
    bridge_cond_wake_all(&bridge->readable);
    bridge_mutex_unlock(&bridge->ring_mutex);
    return input_size;
}

static bool bridge_ensure_header(AirPlayStreamBridge *bridge)
{
    if (bridge->header_written)
        return true;
    if (avformat_write_header(bridge->format, NULL) < 0)
        return false;
    avio_flush(bridge->avio);
    bridge->header_written = true;
    return true;
}

int64_t airplay_stream_bridge_ntp_to_90k(uint64_t ntp_timestamp)
{
    uint64_t seconds = ntp_timestamp >> 32;
    uint64_t fraction = ntp_timestamp & UINT32_MAX;
    uint64_t ticks = seconds * UINT64_C(90000) +
                     ((fraction * UINT64_C(90000)) >> 32);

    return ticks > INT64_MAX ? INT64_MAX : (int64_t)ticks;
}

static void bridge_destroy(AirPlayStreamBridge *bridge)
{
    if (!bridge)
        return;
    airplay_stream_bridge_cancel(bridge);
    if (bridge->format)
    {
        bridge->format->pb = NULL;
        avformat_free_context(bridge->format);
    }
    if (bridge->avio)
    {
        av_freep(&bridge->avio->buffer);
        avio_context_free(&bridge->avio);
    }
    free(bridge->ring);
    bridge_cond_destroy(&bridge->readable);
    bridge_cond_destroy(&bridge->writable);
    bridge_mutex_destroy(&bridge->ring_mutex);
    bridge_mutex_destroy(&bridge->mux_mutex);
    memset(bridge, 0, sizeof(*bridge));
    free(bridge);
}

bool airplay_stream_bridge_create(size_t capacity,
                                  AirPlayStreamBridge **bridge_out)
{
    AirPlayStreamBridge *bridge;
    AVStream *stream;
    uint8_t *avio_buffer;
    bool ring_mutex_ready = false;
    bool mux_mutex_ready = false;
    bool readable_ready = false;
    bool writable_ready = false;

    if (!bridge_out || *bridge_out)
        return false;
    if (capacity == 0u)
        capacity = AIRPLAY_STREAM_BRIDGE_DEFAULT_CAPACITY;
    if (capacity < AIRPLAY_STREAM_BRIDGE_MIN_CAPACITY)
        return false;
    bridge = calloc(1, sizeof(*bridge));
    if (!bridge)
        return false;
    atomic_init(&bridge->references, 1u);
    atomic_init(&bridge->cancelled, false);
    bridge->capacity = capacity;
    bridge->last_pts = -1;
    bridge->last_audio_pts = -1;
    bridge->audio_stream_index = -1;
    ring_mutex_ready = bridge_mutex_init(&bridge->ring_mutex);
    mux_mutex_ready = ring_mutex_ready && bridge_mutex_init(&bridge->mux_mutex);
    readable_ready = mux_mutex_ready && bridge_cond_init(&bridge->readable);
    writable_ready = readable_ready && bridge_cond_init(&bridge->writable);
    bridge->ring = writable_ready ? malloc(capacity) : NULL;
    if (!bridge->ring ||
        avformat_alloc_output_context2(&bridge->format, NULL, "mpegts", NULL) < 0)
        goto failure;
    avio_buffer = av_malloc(AIRPLAY_STREAM_BRIDGE_AVIO_SIZE);
    if (!avio_buffer)
        goto failure;
    bridge->avio = avio_alloc_context(avio_buffer, AIRPLAY_STREAM_BRIDGE_AVIO_SIZE,
                                      1, bridge, NULL, bridge_avio_write, NULL);
    if (!bridge->avio)
    {
        av_free(avio_buffer);
        goto failure;
    }
    bridge->format->pb = bridge->avio;
    bridge->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    stream = avformat_new_stream(bridge->format, NULL);
    if (!stream)
        goto failure;
    bridge->stream_index = stream->index;
    stream->time_base = (AVRational){1, 90000};
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->codec_tag = 0u;
    *bridge_out = bridge;
    return true;

failure:
    if (bridge->format)
    {
        bridge->format->pb = NULL;
        avformat_free_context(bridge->format);
    }
    if (bridge->avio)
    {
        av_freep(&bridge->avio->buffer);
        avio_context_free(&bridge->avio);
    }
    free(bridge->ring);
    if (writable_ready)
        bridge_cond_destroy(&bridge->writable);
    if (readable_ready)
        bridge_cond_destroy(&bridge->readable);
    if (mux_mutex_ready)
        bridge_mutex_destroy(&bridge->mux_mutex);
    if (ring_mutex_ready)
        bridge_mutex_destroy(&bridge->ring_mutex);
    free(bridge);
    return false;
}

void airplay_stream_bridge_retain(AirPlayStreamBridge *bridge)
{
    if (bridge)
        (void)atomic_fetch_add(&bridge->references, 1u);
}

void airplay_stream_bridge_release(AirPlayStreamBridge *bridge)
{
    if (bridge && atomic_fetch_sub(&bridge->references, 1u) == 1u)
        bridge_destroy(bridge);
}

bool airplay_stream_bridge_claim_reader(AirPlayStreamBridge *bridge)
{
    bool claimed = false;

    if (!bridge)
        return false;
    bridge_mutex_lock(&bridge->ring_mutex);
    if (!bridge->reader_claimed && !atomic_load(&bridge->cancelled))
    {
        bridge->reader_claimed = true;
        claimed = true;
    }
    bridge_mutex_unlock(&bridge->ring_mutex);
    return claimed;
}

void airplay_stream_bridge_release_reader(AirPlayStreamBridge *bridge)
{
    if (!bridge)
        return;
    bridge_mutex_lock(&bridge->ring_mutex);
    bridge->reader_claimed = false;
    bridge_mutex_unlock(&bridge->ring_mutex);
}

bool airplay_stream_bridge_push_video(AirPlayStreamBridge *bridge,
                                      const AirPlayMirrorAccessUnit *access_unit)
{
    AVPacket *packet;
    int64_t pts;
    bool ok = false;

    if (!bridge || !access_unit || !access_unit->data || access_unit->size == 0u ||
        access_unit->size > INT_MAX)
        return false;
    bridge_mutex_lock(&bridge->mux_mutex);
    if (bridge->mux_finished || atomic_load(&bridge->cancelled))
        goto cleanup;
    if (access_unit->config_generation < bridge->video_config_generation ||
        (access_unit->config_generation > bridge->video_config_generation &&
         !access_unit->keyframe))
        goto cleanup;
    if (!bridge_ensure_header(bridge))
        goto cleanup;
    packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, (int)access_unit->size) < 0)
    {
        av_packet_free(&packet);
        goto cleanup;
    }
    memcpy(packet->data, access_unit->data, access_unit->size);
    pts = airplay_stream_bridge_ntp_to_90k(access_unit->timestamp);
    if (bridge->last_pts == INT64_MAX)
    {
        av_packet_free(&packet);
        goto cleanup;
    }
    if (pts <= bridge->last_pts)
        pts = bridge->last_pts + 1;
    packet->pts = pts;
    packet->dts = pts;
    packet->stream_index = bridge->stream_index;
    if (access_unit->keyframe)
        packet->flags |= AV_PKT_FLAG_KEY;
    ok = av_interleaved_write_frame(bridge->format, packet) >= 0;
    av_packet_free(&packet);
    if (ok)
    {
        avio_flush(bridge->avio);
        bridge->last_pts = pts;
        bridge->video_config_generation = access_unit->config_generation;
        bridge->video_packets++;
    }

cleanup:
    bridge_mutex_unlock(&bridge->mux_mutex);
    return ok;
}

bool airplay_stream_bridge_configure_audio(
    AirPlayStreamBridge *bridge, const AirPlayMirrorAudioFormat *format)
{
    AVStream *stream;
    uint8_t *extradata = NULL;
    bool ok = false;

    if (!bridge || !format || format->sample_rate == 0u || format->channels == 0u ||
        format->codec_config_size == 0u ||
        format->codec_config_size > sizeof(format->codec_config))
        return false;
    bridge_mutex_lock(&bridge->mux_mutex);
    if (bridge->header_written || bridge->mux_finished ||
        atomic_load(&bridge->cancelled) || bridge->audio_stream_index >= 0)
        goto cleanup;
    extradata = av_mallocz(format->codec_config_size +
                           AV_INPUT_BUFFER_PADDING_SIZE);
    if (!extradata)
        goto cleanup;
    memcpy(extradata, format->codec_config, format->codec_config_size);
    stream = avformat_new_stream(bridge->format, NULL);
    if (!stream)
        goto cleanup;
    stream->time_base = (AVRational){1, (int)format->sample_rate};
    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->codec_id = AV_CODEC_ID_AAC;
    stream->codecpar->codec_tag = 0u;
    stream->codecpar->sample_rate = (int)format->sample_rate;
    av_channel_layout_default(&stream->codecpar->ch_layout, format->channels);
    stream->codecpar->extradata = extradata;
    extradata = NULL;
    stream->codecpar->extradata_size = (int)format->codec_config_size;
    bridge->audio_stream_index = stream->index;
    ok = true;

cleanup:
    av_free(extradata);
    bridge_mutex_unlock(&bridge->mux_mutex);
    return ok;
}

bool airplay_stream_bridge_push_audio(AirPlayStreamBridge *bridge,
                                      const AirPlayMirrorAudioFrame *frame)
{
    AVPacket *packet = NULL;
    int64_t pts;
    bool ok = false;

    if (!bridge || !frame || !frame->data || frame->size == 0u ||
        frame->size > INT_MAX)
        return false;
    bridge_mutex_lock(&bridge->mux_mutex);
    if (bridge->audio_stream_index < 0 || bridge->mux_finished ||
        atomic_load(&bridge->cancelled) || !bridge_ensure_header(bridge))
        goto cleanup;
    packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, (int)frame->size) < 0)
        goto cleanup;
    memcpy(packet->data, frame->data, frame->size);
    pts = frame->rtp_timestamp;
    if (pts <= bridge->last_audio_pts)
        pts = bridge->last_audio_pts == INT64_MAX ? INT64_MAX :
                                                    bridge->last_audio_pts + 1;
    if (pts == INT64_MAX)
        goto cleanup;
    packet->pts = pts;
    packet->dts = pts;
    packet->stream_index = bridge->audio_stream_index;
    ok = av_interleaved_write_frame(bridge->format, packet) >= 0;
    if (ok)
    {
        avio_flush(bridge->avio);
        bridge->last_audio_pts = pts;
        bridge->audio_packets++;
    }

cleanup:
    av_packet_free(&packet);
    bridge_mutex_unlock(&bridge->mux_mutex);
    return ok;
}

bool airplay_stream_bridge_finish(AirPlayStreamBridge *bridge)
{
    bool ok = true;

    if (!bridge)
        return false;
    bridge_mutex_lock(&bridge->mux_mutex);
    if (!bridge->mux_finished && !atomic_load(&bridge->cancelled))
    {
        ok = bridge_ensure_header(bridge) &&
             av_write_trailer(bridge->format) >= 0;
        if (bridge->header_written)
            avio_flush(bridge->avio);
        bridge->mux_finished = true;
    }
    else if (atomic_load(&bridge->cancelled))
        ok = false;
    bridge_mutex_unlock(&bridge->mux_mutex);
    bridge_mutex_lock(&bridge->ring_mutex);
    bridge->eof = !atomic_load(&bridge->cancelled);
    bridge_cond_wake_all(&bridge->readable);
    bridge_cond_wake_all(&bridge->writable);
    bridge_mutex_unlock(&bridge->ring_mutex);
    return ok;
}

void airplay_stream_bridge_cancel(AirPlayStreamBridge *bridge)
{
    if (!bridge)
        return;
    bridge_mutex_lock(&bridge->ring_mutex);
    atomic_store(&bridge->cancelled, true);
    bridge_cond_wake_all(&bridge->readable);
    bridge_cond_wake_all(&bridge->writable);
    bridge_mutex_unlock(&bridge->ring_mutex);
}

int64_t airplay_stream_bridge_read(AirPlayStreamBridge *bridge,
                                   uint8_t *output, size_t output_size)
{
    size_t amount;
    size_t first;

    if (!bridge || !output || output_size == 0u || output_size > INT64_MAX)
        return -1;
    bridge_mutex_lock(&bridge->ring_mutex);
    while (bridge->buffered == 0u && !bridge->eof &&
           !atomic_load(&bridge->cancelled))
        bridge_cond_wait(&bridge->readable, &bridge->ring_mutex);
    if (atomic_load(&bridge->cancelled))
    {
        bridge_mutex_unlock(&bridge->ring_mutex);
        return -1;
    }
    if (bridge->buffered == 0u && bridge->eof)
    {
        bridge_mutex_unlock(&bridge->ring_mutex);
        return 0;
    }
    amount = output_size < bridge->buffered ? output_size : bridge->buffered;
    first = bridge->capacity - bridge->read_offset;
    if (first > amount)
        first = amount;
    memcpy(output, bridge->ring + bridge->read_offset, first);
    memcpy(output + first, bridge->ring, amount - first);
    bridge->read_offset = (bridge->read_offset + amount) % bridge->capacity;
    bridge->buffered -= amount;
    bridge->bytes_read += amount;
    bridge_cond_wake_all(&bridge->writable);
    bridge_mutex_unlock(&bridge->ring_mutex);
    return (int64_t)amount;
}

bool airplay_stream_bridge_get_stats(AirPlayStreamBridge *bridge,
                                     AirPlayStreamBridgeStats *stats_out)
{
    if (!bridge || !stats_out)
        return false;
    bridge_mutex_lock(&bridge->mux_mutex);
    bridge_mutex_lock(&bridge->ring_mutex);
    stats_out->capacity = bridge->capacity;
    stats_out->buffered = bridge->buffered;
    stats_out->bytes_written = bridge->bytes_written;
    stats_out->bytes_read = bridge->bytes_read;
    stats_out->video_packets = bridge->video_packets;
    stats_out->audio_packets = bridge->audio_packets;
    stats_out->video_config_generation = bridge->video_config_generation;
    stats_out->eof = bridge->eof;
    stats_out->cancelled = atomic_load(&bridge->cancelled);
    stats_out->reader_claimed = bridge->reader_claimed;
    bridge_mutex_unlock(&bridge->ring_mutex);
    bridge_mutex_unlock(&bridge->mux_mutex);
    return true;
}
