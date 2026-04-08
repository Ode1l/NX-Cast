#pragma once

#include "player.h"

typedef PlayerBackendType RendererBackendType;
typedef PlayerMedia RendererMedia;
typedef PlayerState RendererState;
typedef PlayerEventType RendererEventType;
typedef PlayerEvent RendererEvent;
typedef PlayerEventCallback RendererEventCallback;
typedef PlayerSnapshot RendererSnapshot;

#define RENDERER_BACKEND_AUTO PLAYER_BACKEND_AUTO
#define RENDERER_BACKEND_MOCK PLAYER_BACKEND_MOCK
#define RENDERER_BACKEND_LIBMPV PLAYER_BACKEND_LIBMPV

static inline bool renderer_set_backend(RendererBackendType backend) { return player_set_backend(backend); }
static inline RendererBackendType renderer_get_backend(void) { return player_get_backend(); }
static inline const char *renderer_get_backend_name(void) { return player_get_backend_name(); }
static inline bool renderer_init(void) { return player_init(); }
static inline void renderer_deinit(void) { player_deinit(); }
static inline void renderer_set_event_callback(RendererEventCallback callback, void *user) { player_set_event_callback(callback, user); }
static inline bool renderer_set_media(const RendererMedia *media) { return player_set_media(media); }
static inline bool renderer_set_uri(const char *uri, const char *metadata) { return player_set_uri(uri, metadata); }
static inline bool renderer_play(void) { return player_play(); }
static inline bool renderer_pause(void) { return player_pause(); }
static inline bool renderer_stop(void) { return player_stop(); }
static inline bool renderer_seek_ms(int position_ms) { return player_seek_ms(position_ms); }
static inline bool renderer_set_volume(int volume_0_100) { return player_set_volume(volume_0_100); }
static inline bool renderer_set_mute(bool mute) { return player_set_mute(mute); }
static inline bool renderer_video_supported(void) { return player_video_supported(); }
static inline bool renderer_video_attach_gl(void *(*get_proc_address)(void *ctx, const char *name), void *get_proc_address_ctx) { return player_video_attach_gl(get_proc_address, get_proc_address_ctx); }
static inline bool renderer_video_attach_sw(void) { return player_video_attach_sw(); }
static inline void renderer_video_detach(void) { player_video_detach(); }
static inline bool renderer_video_render_gl(int fbo, int width, int height, bool flip_y) { return player_video_render_gl(fbo, width, height, flip_y); }
static inline bool renderer_video_render_sw(void *pixels, int width, int height, size_t stride) { return player_video_render_sw(pixels, width, height, stride); }
static inline int renderer_get_position_ms(void) { return player_get_position_ms(); }
static inline int renderer_get_duration_ms(void) { return player_get_duration_ms(); }
static inline int renderer_get_volume(void) { return player_get_volume(); }
static inline bool renderer_get_mute(void) { return player_get_mute(); }
static inline bool renderer_is_seekable(void) { return player_is_seekable(); }
static inline RendererState renderer_get_state(void) { return player_get_state(); }
static inline bool renderer_get_current_media(RendererMedia *out) { return player_get_current_media(out); }
static inline bool renderer_get_snapshot(RendererSnapshot *out) { return player_get_snapshot(out); }
static inline void renderer_media_clear(RendererMedia *media) { player_media_clear(media); }
static inline void renderer_snapshot_clear(RendererSnapshot *snapshot) { player_snapshot_clear(snapshot); }
