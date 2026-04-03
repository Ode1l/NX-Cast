#pragma once

#include "player/types.h"

#ifdef HAVE_LIBMPV
#include <mpv/client.h>

typedef enum
{
    LIBMPV_HLS_RUNTIME_UNKNOWN = 0,
    LIBMPV_HLS_RUNTIME_VOD,
    LIBMPV_HLS_RUNTIME_LIVE
} LibmpvHlsRuntimeKind;

const char *libmpv_hls_runtime_kind_name(LibmpvHlsRuntimeKind kind);
LibmpvHlsRuntimeKind libmpv_hls_detect_runtime_kind(bool live_hint,
                                                    bool media_loaded,
                                                    bool seekable,
                                                    int duration_ms,
                                                    const char *stream_path,
                                                    const char *demuxer);
void libmpv_hls_log_cache_state_node(const mpv_node *node);

#endif
