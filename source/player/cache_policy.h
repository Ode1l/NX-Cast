#ifndef NXCAST_PLAYER_CACHE_POLICY_H
#define NXCAST_PLAYER_CACHE_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#ifndef NXCAST_MEDIA_CACHE_FORWARD_MIB
#define NXCAST_MEDIA_CACHE_FORWARD_MIB 20
#endif
#ifndef NXCAST_MEDIA_CACHE_BACKWARD_MIB
#define NXCAST_MEDIA_CACHE_BACKWARD_MIB 10
#endif
#ifndef NXCAST_MEDIA_CACHE_READAHEAD_SECS
#define NXCAST_MEDIA_CACHE_READAHEAD_SECS 20
#endif

#if NXCAST_MEDIA_CACHE_FORWARD_MIB <= 0
#error "NXCAST_MEDIA_CACHE_FORWARD_MIB must be positive"
#endif
#if NXCAST_MEDIA_CACHE_BACKWARD_MIB < 0 ||                            \
    NXCAST_MEDIA_CACHE_BACKWARD_MIB > NXCAST_MEDIA_CACHE_FORWARD_MIB
#error "NXCAST_MEDIA_CACHE_BACKWARD_MIB must be between 0 and the forward cache"
#endif
#if NXCAST_MEDIA_CACHE_READAHEAD_SECS <= 0
#error "NXCAST_MEDIA_CACHE_READAHEAD_SECS must be positive"
#endif

typedef enum
{
    PLAYER_CACHE_POLICY_NETWORK = 0,
    PLAYER_CACHE_POLICY_AIRPLAY_MIRROR
} PlayerCachePolicyKind;

typedef struct
{
    PlayerCachePolicyKind kind;
    bool cache_enabled;
    unsigned int forward_mib;
    unsigned int backward_mib;
    unsigned int readahead_secs;
} PlayerCachePolicy;

PlayerCachePolicy player_cache_policy_for_uri(const char *uri);
const char *player_cache_policy_name(PlayerCachePolicyKind kind);
bool player_cache_policy_format_options(const PlayerCachePolicy *policy,
                                        bool paused,
                                        char *output,
                                        size_t output_size);

#endif
