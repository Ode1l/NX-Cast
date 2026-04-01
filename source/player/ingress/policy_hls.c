#include "player/policy.h"

#include <stdio.h>

#define SOURCE_POLICY_HLS_LOAD_OPTIONS \
    "demuxer-lavf-probe-info=nostreams,demuxer-lavf-analyzeduration=0.15,demuxer-lavf-probescore=16,cache-pause-initial=yes,cache-pause-wait=3"

void policy_apply_hls(PlayerMedia *media)
{
    if (!media || !media->flags.is_hls)
        return;

    media->profile = PLAYER_MEDIA_PROFILE_GENERIC_HLS;
    media->network_timeout_seconds = 15;
    snprintf(media->probe_info, sizeof(media->probe_info), "%s", "nostreams");
    snprintf(media->mpv_load_options, sizeof(media->mpv_load_options), "%s", SOURCE_POLICY_HLS_LOAD_OPTIONS);
}
