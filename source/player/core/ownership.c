#include "ownership.h"

#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex PlayerOwnershipMutex;
#else
#include <pthread.h>
typedef pthread_mutex_t PlayerOwnershipMutex;
#endif

static PlayerOwnershipMutex g_mutex;
static PlayerOwnershipMutex g_transition_mutex;
static PlayerOwnershipLease g_current;
static uint32_t g_next_generation;
static bool g_ready;

static void ownership_init(void)
{
    if (g_ready)
        return;
#ifdef __SWITCH__
    mutexInit(&g_mutex);
    mutexInit(&g_transition_mutex);
#else
    (void)pthread_mutex_init(&g_mutex, NULL);
    (void)pthread_mutex_init(&g_transition_mutex, NULL);
#endif
    g_ready = true;
}

static void ownership_lock(void)
{
    ownership_init();
#ifdef __SWITCH__
    mutexLock(&g_mutex);
#else
    pthread_mutex_lock(&g_mutex);
#endif
}

static void ownership_unlock(void)
{
#ifdef __SWITCH__
    mutexUnlock(&g_mutex);
#else
    pthread_mutex_unlock(&g_mutex);
#endif
}

void player_ownership_transition_begin(void)
{
    ownership_init();
#ifdef __SWITCH__
    mutexLock(&g_transition_mutex);
#else
    pthread_mutex_lock(&g_transition_mutex);
#endif
}

void player_ownership_transition_end(void)
{
#ifdef __SWITCH__
    mutexUnlock(&g_transition_mutex);
#else
    pthread_mutex_unlock(&g_transition_mutex);
#endif
}

static uint32_t next_generation(void)
{
    ++g_next_generation;
    if (g_next_generation == 0u)
        ++g_next_generation;
    return g_next_generation;
}

static bool lease_equal(const PlayerOwnershipLease *left,
                        const PlayerOwnershipLease *right)
{
    return left->owner == right->owner && left->token == right->token &&
           left->generation == right->generation;
}

bool player_ownership_claim(PlayerMediaOwner owner, uint64_t token,
                            PlayerOwnershipLease *lease_out,
                            PlayerOwnershipLease *previous_out)
{
    if (owner == PLAYER_MEDIA_OWNER_NONE || !lease_out)
        return false;

    ownership_lock();
    if (previous_out)
        *previous_out = g_current;
    g_current.owner = owner;
    g_current.token = token;
    g_current.generation = next_generation();
    *lease_out = g_current;
    ownership_unlock();
    return true;
}

bool player_ownership_current(PlayerOwnershipLease *lease_out)
{
    if (!lease_out)
        return false;

    ownership_lock();
    *lease_out = g_current;
    ownership_unlock();
    return lease_out->owner != PLAYER_MEDIA_OWNER_NONE;
}

bool player_ownership_validate(const PlayerOwnershipLease *lease)
{
    bool valid;

    if (!lease || lease->owner == PLAYER_MEDIA_OWNER_NONE ||
        lease->generation == 0u)
        return false;
    ownership_lock();
    valid = lease_equal(lease, &g_current);
    ownership_unlock();
    return valid;
}

bool player_ownership_matches(PlayerMediaOwner owner, uint64_t token)
{
    bool matches;

    if (owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    ownership_lock();
    matches = g_current.owner == owner && g_current.token == token;
    ownership_unlock();
    return matches;
}

bool player_ownership_release(const PlayerOwnershipLease *lease)
{
    bool released = false;

    if (!lease)
        return false;
    ownership_lock();
    if (lease_equal(lease, &g_current))
    {
        memset(&g_current, 0, sizeof(g_current));
        released = true;
    }
    ownership_unlock();
    return released;
}

bool player_ownership_release_current(PlayerMediaOwner owner, uint64_t token)
{
    bool released = false;

    ownership_lock();
    if (g_current.owner == owner && g_current.token == token)
    {
        memset(&g_current, 0, sizeof(g_current));
        released = true;
    }
    ownership_unlock();
    return released;
}

void player_ownership_reset(void)
{
    ownership_lock();
    memset(&g_current, 0, sizeof(g_current));
    (void)next_generation();
    ownership_unlock();
}

const char *player_media_owner_name(PlayerMediaOwner owner)
{
    switch (owner)
    {
    case PLAYER_MEDIA_OWNER_DLNA:
        return "dlna";
    case PLAYER_MEDIA_OWNER_IPTV:
        return "iptv";
    case PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO:
        return "airplay-video";
    case PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR:
        return "airplay-mirror";
    case PLAYER_MEDIA_OWNER_NONE:
    default:
        return "none";
    }
}
