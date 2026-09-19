#include "player/cache_policy.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "player/backend/libmpv_airplay.h"

#define AIRPLAY_REMOTE_HLS_LOCAL_PREFIX "/airplay-hls/"

static bool is_local_airplay_hls_uri(const char *uri)
{
    const char *path;

    if (!uri || strncmp(uri, "http://127.0.0.1:", 17u) != 0)
        return false;
    path = strchr(uri + 17u, '/');
    return path && strncmp(path, AIRPLAY_REMOTE_HLS_LOCAL_PREFIX,
                           sizeof(AIRPLAY_REMOTE_HLS_LOCAL_PREFIX) - 1u) == 0;
}

static bool is_direct_mp4_uri(const char *uri)
{
    const char *end;

    if (!uri || uri[0] == '\0')
        return false;
    end = strpbrk(uri, "?#");
    if (!end)
        end = uri + strlen(uri);
    return end - uri >= 4 && strncasecmp(end - 4, ".mp4", 4u) == 0;
}

PlayerCachePolicy player_cache_policy_for_uri(const char *uri)
{
    PlayerCachePolicy policy = {
        .kind = PLAYER_CACHE_POLICY_NETWORK,
        .cache_enabled = true,
    };

    if (uri && strcmp(uri, PLAYER_LIBMPV_AIRPLAY_URI) == 0)
    {
        policy.kind = PLAYER_CACHE_POLICY_AIRPLAY_MIRROR;
        policy.configure_cache = true;
        policy.cache_enabled = false;
        policy.forward_mib = 0;
        policy.backward_mib = 0;
        policy.readahead_secs = 0;
    }
    else if (is_local_airplay_hls_uri(uri))
    {
        policy.kind = PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS;
        policy.configure_cache = true;
        policy.forward_mib = NXCAST_MEDIA_CACHE_FORWARD_MIB;
        policy.backward_mib = NXCAST_MEDIA_CACHE_BACKWARD_MIB;
        policy.readahead_secs = NXCAST_MEDIA_CACHE_READAHEAD_SECS;
        policy.disable_http_persistence = true;
    }
    else if (is_direct_mp4_uri(uri))
    {
        policy.kind = PLAYER_CACHE_POLICY_DIRECT_MP4;
        policy.configure_cache = true;
        policy.forward_mib = 8u;
        policy.backward_mib = 2u;
        policy.readahead_secs = 2u;
    }
    return policy;
}

const char *player_cache_policy_name(PlayerCachePolicyKind kind)
{
    switch (kind)
    {
    case PLAYER_CACHE_POLICY_NETWORK:
        return "network-default";
    case PLAYER_CACHE_POLICY_DIRECT_MP4:
        return "direct-mp4-fast";
    case PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS:
        return "airplay-reverse-hls";
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
    if (!policy->configure_cache)
    {
        written = snprintf(output, output_size, "pause=%s",
                           paused ? "yes" : "no");
    }
    else if (!policy->cache_enabled)
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
            "demuxer-max-back-bytes=%uMiB%s",
            paused ? "yes" : "no", policy->readahead_secs,
            policy->forward_mib, policy->backward_mib,
            policy->disable_http_persistence
                ? ",demuxer-lavf-o=http_persistent=no"
                : "");
    }
    return written >= 0 && (size_t)written < output_size;
}
