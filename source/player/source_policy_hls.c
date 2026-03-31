#include "source_policy.h"

#include <stdio.h>

void source_policy_apply_hls(PlayerResolvedSource *source)
{
    if (!source || !source->flags.is_hls)
        return;

    source->profile = PLAYER_SOURCE_PROFILE_GENERIC_HLS;
    snprintf(source->probe_info, sizeof(source->probe_info), "%s", "nostreams");
}
