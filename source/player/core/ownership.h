#ifndef NXCAST_PLAYER_CORE_OWNERSHIP_H
#define NXCAST_PLAYER_CORE_OWNERSHIP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PLAYER_MEDIA_OWNER_NONE = 0,
    PLAYER_MEDIA_OWNER_DLNA,
    PLAYER_MEDIA_OWNER_IPTV,
    PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
    PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR
} PlayerMediaOwner;

typedef struct
{
    PlayerMediaOwner owner;
    uint64_t token;
    uint32_t generation;
} PlayerOwnershipLease;

bool player_ownership_claim(PlayerMediaOwner owner, uint64_t token,
                            PlayerOwnershipLease *lease_out,
                            PlayerOwnershipLease *previous_out);
bool player_ownership_current(PlayerOwnershipLease *lease_out);
bool player_ownership_validate(const PlayerOwnershipLease *lease);
bool player_ownership_matches(PlayerMediaOwner owner, uint64_t token);
bool player_ownership_release(const PlayerOwnershipLease *lease);
bool player_ownership_release_current(PlayerMediaOwner owner, uint64_t token);
void player_ownership_reset(void);
const char *player_media_owner_name(PlayerMediaOwner owner);

#endif
