#include "player/policy.h"
#include "player/ingress/hls.h"

#include <stdio.h>

#define SOURCE_POLICY_HLS_LOAD_OPTIONS_BASE \
    "demuxer-lavf-probe-info=nostreams,demuxer-lavf-analyzeduration=0.15,demuxer-lavf-probescore=16,cache-pause-initial=yes"

void policy_apply_hls(PlayerMedia *media)
{
    int cache_pause_wait_seconds;

    if (!media || media->format != PLAYER_MEDIA_FORMAT_HLS)
        return;

    media->profile = PLAYER_MEDIA_PROFILE_GENERIC_HLS;
    snprintf(media->probe_info, sizeof(media->probe_info), "%s", "nostreams");

    media->network_timeout_seconds = 15;
    media->demuxer_readahead_seconds = ingress_hls_default_readahead_seconds(media->flags.likely_live);
    cache_pause_wait_seconds = ingress_hls_cache_pause_wait_seconds(media->flags.likely_live);
    snprintf(media->mpv_load_options,
             sizeof(media->mpv_load_options),
             "%s,cache-pause-wait=%d",
             SOURCE_POLICY_HLS_LOAD_OPTIONS_BASE,
             cache_pause_wait_seconds);

    snprintf(media->format_hint,
             sizeof(media->format_hint),
             "%s",
             media->flags.likely_live ? "hls-live" : "hls-vod");
}
