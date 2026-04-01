#include "source_policy.h"

#include <stdio.h>

#define SOURCE_POLICY_HLS_LOAD_OPTIONS \
    "demuxer-lavf-probe-info=nostreams,demuxer-lavf-analyzeduration=0.15,demuxer-lavf-probescore=16,cache-pause-initial=yes,cache-pause-wait=3"

void source_policy_apply_hls(PlayerResolvedSource *source)
{
    if (!source || !source->flags.is_hls)
        return;

    source->profile = PLAYER_SOURCE_PROFILE_GENERIC_HLS;
    source->network_timeout_seconds = 15;
    snprintf(source->probe_info, sizeof(source->probe_info), "%s", "nostreams");
    snprintf(source->mpv_load_options, sizeof(source->mpv_load_options), "%s", SOURCE_POLICY_HLS_LOAD_OPTIONS);
}
