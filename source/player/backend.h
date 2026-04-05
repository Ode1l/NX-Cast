#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ingress.h"
#include "types.h"

typedef struct
{
    const char *name;
    bool (*available)(void);
    bool (*init)(void);
    void (*deinit)(void);
    void (*set_event_sink)(void (*sink)(const PlayerEvent *event));

    bool (*set_media)(const PlayerMedia *media);
    bool (*play)(void);
    bool (*pause)(void);
    bool (*stop)(void);
    bool (*seek_ms)(int position_ms);
    bool (*set_volume)(int volume_0_100);
    bool (*set_mute)(bool mute);
    bool (*pump_events)(int timeout_ms);
    void (*wakeup)(void);
    bool (*render_supported)(void);
    bool (*render_attach_gl)(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx);
    bool (*render_attach_sw)(void);
    void (*render_detach)(void);
    bool (*render_frame_gl)(int fbo, int width, int height, bool flip_y);
    bool (*render_frame_sw)(void *pixels, int width, int height, size_t stride);

    int (*get_position_ms)(void);
    int (*get_duration_ms)(void);
    int (*get_volume)(void);
    bool (*get_mute)(void);
    bool (*is_seekable)(void);
    PlayerState (*get_state)(void);
} BackendOps;

extern const BackendOps g_mock_ops;
extern const BackendOps g_libmpv_ops;
