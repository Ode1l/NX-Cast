#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

bool player_init(void);
void player_deinit(void);

void player_set_event_callback(PlayerEventCallback callback, void *user);

bool player_set_uri(const char *uri, const char *metadata);
bool player_set_uri_with_context(const char *uri, const char *metadata, const PlayerOpenContext *ctx);
bool player_play(void);
bool player_pause(void);
bool player_stop(void);
bool player_seek_ms(int position_ms);
bool player_set_volume(int volume_0_100);
bool player_set_mute(bool mute);
bool player_video_supported(void);
bool player_video_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx);
bool player_video_attach_sw(void);
void player_video_detach(void);
bool player_video_render_gl(int fbo, int width, int height, bool flip_y);
bool player_video_render_sw(void *pixels, int width, int height, size_t stride);

int player_get_position_ms(void);
int player_get_duration_ms(void);
int player_get_volume(void);
bool player_get_mute(void);
bool player_is_seekable(void);
PlayerState player_get_state(void);
bool player_get_snapshot(PlayerSnapshot *out);

/*
 * Public enum-to-name helpers for protocol/render logging. External layers
 * should not depend on ingress internals.
 */
const char *player_media_vendor_name(PlayerMediaVendor vendor);
const char *player_media_format_name(PlayerMediaFormat format);
const char *player_media_transport_name(PlayerMediaTransport transport);
