#include "video.h"

#include <stdlib.h>
#include <string.h>

struct AirPlayMirrorVideo
{
    AirPlayMirrorVideoCallback callback;
    void *user_data;
    uint8_t config[AIRPLAY_MIRROR_VIDEO_MAX_CONFIG];
    size_t config_size;
    uint32_t config_generation;
    uint64_t config_timestamp;
    uint64_t last_timestamp;
    bool has_last_timestamp;
    bool waiting_for_keyframe;
};

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool append_nal(uint8_t *output, size_t capacity, size_t *offset,
                       const uint8_t *nal, size_t nal_size)
{
    static const uint8_t start_code[4] = {0u, 0u, 0u, 1u};

    if (!output || !offset || !nal || nal_size == 0u || *offset > capacity ||
        nal_size + sizeof(start_code) > capacity - *offset || (nal[0] & 0x80u) != 0u)
        return false;
    memcpy(output + *offset, start_code, sizeof(start_code));
    *offset += sizeof(start_code);
    memcpy(output + *offset, nal, nal_size);
    *offset += nal_size;
    return true;
}

static bool parse_parameter_sets(const uint8_t *avcc, size_t avcc_size,
                                 uint8_t *output, size_t *output_size)
{
    size_t input_offset = 6u;
    size_t output_offset = 0u;
    unsigned sps_count;
    unsigned pps_count;

    if (!avcc || !output || !output_size || avcc_size < 7u || avcc[0] != 1u ||
        (avcc[4] & 0x03u) != 3u)
        return false;
    sps_count = avcc[5] & 0x1fu;
    if (sps_count == 0u)
        return false;
    for (unsigned index = 0u; index < sps_count; ++index)
    {
        size_t nal_size;

        if (input_offset + 2u > avcc_size)
            return false;
        nal_size = read_be16(avcc + input_offset);
        input_offset += 2u;
        if (nal_size == 0u || input_offset + nal_size > avcc_size ||
            (avcc[input_offset] & 0x1fu) != 7u ||
            !append_nal(output, AIRPLAY_MIRROR_VIDEO_MAX_CONFIG, &output_offset,
                        avcc + input_offset, nal_size))
            return false;
        input_offset += nal_size;
    }
    if (input_offset >= avcc_size)
        return false;
    pps_count = avcc[input_offset++];
    if (pps_count == 0u)
        return false;
    for (unsigned index = 0u; index < pps_count; ++index)
    {
        size_t nal_size;

        if (input_offset + 2u > avcc_size)
            return false;
        nal_size = read_be16(avcc + input_offset);
        input_offset += 2u;
        if (nal_size == 0u || input_offset + nal_size > avcc_size ||
            (avcc[input_offset] & 0x1fu) != 8u ||
            !append_nal(output, AIRPLAY_MIRROR_VIDEO_MAX_CONFIG, &output_offset,
                        avcc + input_offset, nal_size))
            return false;
        input_offset += nal_size;
    }
    if (input_offset != avcc_size)
        return false;
    *output_size = output_offset;
    return true;
}

bool airplay_mirror_video_create(AirPlayMirrorVideoCallback callback,
                                 void *user_data,
                                 AirPlayMirrorVideo **video_out)
{
    AirPlayMirrorVideo *video;

    if (!callback || !video_out || *video_out)
        return false;
    video = calloc(1, sizeof(*video));
    if (!video)
        return false;
    video->callback = callback;
    video->user_data = user_data;
    video->waiting_for_keyframe = true;
    *video_out = video;
    return true;
}

void airplay_mirror_video_destroy(AirPlayMirrorVideo *video)
{
    if (!video)
        return;
    memset(video, 0, sizeof(*video));
    free(video);
}

void airplay_mirror_video_reset(AirPlayMirrorVideo *video)
{
    if (!video)
        return;
    video->has_last_timestamp = false;
    video->last_timestamp = 0u;
    video->waiting_for_keyframe = true;
}

AirPlayMirrorVideoResult airplay_mirror_video_process_config(
    AirPlayMirrorVideo *video, const uint8_t *avcc, size_t avcc_size,
    uint64_t timestamp)
{
    uint8_t parsed[AIRPLAY_MIRROR_VIDEO_MAX_CONFIG];
    size_t parsed_size = 0u;

    if (!video || avcc_size > AIRPLAY_MIRROR_VIDEO_MAX_CONFIG ||
        !parse_parameter_sets(avcc, avcc_size, parsed, &parsed_size))
        return AIRPLAY_MIRROR_VIDEO_INVALID;
    if (parsed_size != video->config_size ||
        memcmp(parsed, video->config, parsed_size) != 0)
    {
        memcpy(video->config, parsed, parsed_size);
        video->config_size = parsed_size;
        video->config_generation++;
        if (video->config_generation == 0u)
            video->config_generation = 1u;
        video->waiting_for_keyframe = true;
    }
    video->config_timestamp = timestamp;
    return AIRPLAY_MIRROR_VIDEO_OK;
}

static bool inspect_access_unit(const uint8_t *payload, size_t payload_size,
                                size_t *annexb_size, bool *keyframe)
{
    size_t offset = 0u;
    size_t output_size = 0u;

    if (!payload || !annexb_size || !keyframe || payload_size == 0u ||
        payload_size > AIRPLAY_MIRROR_VIDEO_MAX_ACCESS_UNIT)
        return false;
    *keyframe = false;
    while (offset < payload_size)
    {
        size_t nal_size;
        uint8_t nal_type;

        if (payload_size - offset < 4u)
            return false;
        nal_size = read_be32(payload + offset);
        offset += 4u;
        if (nal_size == 0u || nal_size > payload_size - offset ||
            nal_size + 4u > AIRPLAY_MIRROR_VIDEO_MAX_ACCESS_UNIT - output_size ||
            (payload[offset] & 0x80u) != 0u)
            return false;
        nal_type = payload[offset] & 0x1fu;
        if (nal_type == 5u)
            *keyframe = true;
        output_size += nal_size + 4u;
        offset += nal_size;
    }
    *annexb_size = output_size;
    return true;
}

static void write_access_unit(const uint8_t *payload, size_t payload_size,
                              uint8_t *output)
{
    static const uint8_t start_code[4] = {0u, 0u, 0u, 1u};
    size_t input_offset = 0u;
    size_t output_offset = 0u;

    while (input_offset < payload_size)
    {
        size_t nal_size = read_be32(payload + input_offset);

        input_offset += 4u;
        memcpy(output + output_offset, start_code, sizeof(start_code));
        output_offset += sizeof(start_code);
        memcpy(output + output_offset, payload + input_offset, nal_size);
        output_offset += nal_size;
        input_offset += nal_size;
    }
}

AirPlayMirrorVideoResult airplay_mirror_video_process_access_unit(
    AirPlayMirrorVideo *video, const uint8_t *payload, size_t payload_size,
    uint64_t timestamp)
{
    AirPlayMirrorAccessUnit access_unit;
    size_t annexb_size;
    size_t prefix_size;
    bool keyframe;
    uint8_t *output;

    if (!video || !inspect_access_unit(payload, payload_size, &annexb_size, &keyframe))
        return AIRPLAY_MIRROR_VIDEO_INVALID;
    if (video->config_size == 0u ||
        (video->has_last_timestamp && timestamp < video->last_timestamp) ||
        (video->waiting_for_keyframe && !keyframe))
        return AIRPLAY_MIRROR_VIDEO_DROPPED;
    prefix_size = video->waiting_for_keyframe ? video->config_size : 0u;
    if (annexb_size > AIRPLAY_MIRROR_VIDEO_MAX_ACCESS_UNIT - prefix_size)
        return AIRPLAY_MIRROR_VIDEO_INVALID;
    output = malloc(prefix_size + annexb_size);
    if (!output)
        return AIRPLAY_MIRROR_VIDEO_NO_MEMORY;
    if (prefix_size)
        memcpy(output, video->config, prefix_size);
    write_access_unit(payload, payload_size, output + prefix_size);
    access_unit.data = output;
    access_unit.size = prefix_size + annexb_size;
    access_unit.timestamp = timestamp;
    access_unit.config_generation = video->config_generation;
    access_unit.keyframe = keyframe;
    video->callback(&access_unit, video->user_data);
    free(output);
    video->waiting_for_keyframe = false;
    video->has_last_timestamp = true;
    video->last_timestamp = timestamp;
    return AIRPLAY_MIRROR_VIDEO_OK;
}

uint32_t airplay_mirror_video_config_generation(const AirPlayMirrorVideo *video)
{
    return video ? video->config_generation : 0u;
}

bool airplay_mirror_video_waiting_for_keyframe(const AirPlayMirrorVideo *video)
{
    return !video || video->waiting_for_keyframe;
}

const char *airplay_mirror_video_result_name(AirPlayMirrorVideoResult result)
{
    switch (result)
    {
    case AIRPLAY_MIRROR_VIDEO_OK:
        return "ok";
    case AIRPLAY_MIRROR_VIDEO_DROPPED:
        return "dropped";
    case AIRPLAY_MIRROR_VIDEO_INVALID:
        return "invalid";
    case AIRPLAY_MIRROR_VIDEO_NO_MEMORY:
        return "no-memory";
    default:
        return "unknown";
    }
}
