#include "player/cache_policy.h"

#include <stdio.h>
#include <string.h>

#include "player/backend/libmpv_airplay.h"

PlayerCachePolicy player_cache_policy_for_uri(const char *uri)
{
    PlayerCachePolicy policy = {
        .kind = PLAYER_CACHE_POLICY_NETWORK,
        .cache_enabled = true,
        .forward_mib = NXCAST_MEDIA_CACHE_FORWARD_MIB,
        .backward_mib = NXCAST_MEDIA_CACHE_BACKWARD_MIB,
        .readahead_secs = NXCAST_MEDIA_CACHE_READAHEAD_SECS,
    };

    if (uri && strcmp(uri, PLAYER_LIBMPV_AIRPLAY_URI) == 0)
    {
        policy.kind = PLAYER_CACHE_POLICY_AIRPLAY_MIRROR;
        policy.cache_enabled = false;
        policy.forward_mib = 0;
        policy.backward_mib = 0;
        policy.readahead_secs = 0;
    }
    return policy;
}

const char *player_cache_policy_name(PlayerCachePolicyKind kind)
{
    switch (kind)
    {
    case PLAYER_CACHE_POLICY_NETWORK:
        return "network-buffered";
    case PLAYER_CACHE_POLICY_AIRPLAY_MIRROR:
        return "airplay-mirror-low-latency";
    default:
        return "unknown";
    }
}

bool player_cache_policy_format_options(const PlayerCachePolicy *policy,
                                        bool paused,
                                        char *output,
                                        size_t output_size)
{
    int written;

    if (!policy || !output || output_size == 0)
        return false;
    if (!policy->cache_enabled)
    {
        written = snprintf(output, output_size, "pause=%s,cache=no",
                           paused ? "yes" : "no");
    }
    else
    {
        written = snprintf(
            output, output_size,
            "pause=%s,cache=yes,cache-pause-initial=no,"
            "demuxer-readahead-secs=%u,demuxer-max-bytes=%uMiB,"
            "demuxer-max-back-bytes=%uMiB",
            paused ? "yes" : "no", policy->readahead_secs,
            policy->forward_mib, policy->backward_mib);
    }
    return written >= 0 && (size_t)written < output_size;
}
