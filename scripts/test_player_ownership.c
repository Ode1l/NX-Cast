#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "player/core/ownership.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

typedef struct
{
    PlayerMediaOwner owner;
    uint64_t token;
    PlayerOwnershipLease lease;
    atomic_bool attempted;
    atomic_bool completed;
} ClaimThread;

static void *claim_thread(void *argument)
{
    ClaimThread *claim = argument;

    atomic_store(&claim->attempted, true);
    player_ownership_transition_begin();
    CHECK(player_ownership_claim(claim->owner, claim->token, &claim->lease,
                                 NULL));
    player_ownership_transition_end();
    atomic_store(&claim->completed, true);
    return NULL;
}

int main(void)
{
    PlayerOwnershipLease dlna = {0};
    PlayerOwnershipLease previous = {0};
    PlayerOwnershipLease current = {0};
    ClaimThread claim = {.owner = PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                         .token = 42u};
    pthread_t thread;
    struct timespec wait = {.tv_sec = 0, .tv_nsec = 20 * 1000 * 1000};

    player_ownership_reset();
    CHECK(!player_ownership_current(&current));
    CHECK(player_ownership_claim(PLAYER_MEDIA_OWNER_DLNA, 1u, &dlna,
                                 &previous));
    CHECK(previous.owner == PLAYER_MEDIA_OWNER_NONE);
    CHECK(player_ownership_validate(&dlna));
    CHECK(player_ownership_matches(PLAYER_MEDIA_OWNER_DLNA, 1u));

    atomic_init(&claim.attempted, false);
    atomic_init(&claim.completed, false);
    player_ownership_transition_begin();
    CHECK(pthread_create(&thread, NULL, claim_thread, &claim) == 0);
    while (!atomic_load(&claim.attempted))
        sched_yield();
    (void)nanosleep(&wait, NULL);
    CHECK(!atomic_load(&claim.completed));
    CHECK(player_ownership_validate(&dlna));
    player_ownership_transition_end();
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(atomic_load(&claim.completed));
    CHECK(player_ownership_validate(&claim.lease));
    CHECK(!player_ownership_validate(&dlna));
    CHECK(!player_ownership_release(&dlna));
    CHECK(player_ownership_matches(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO, 42u));

    CHECK(player_ownership_release(&claim.lease));
    CHECK(!player_ownership_current(&current));
    CHECK(!player_ownership_release(&claim.lease));
    CHECK(strcmp(player_media_owner_name(PLAYER_MEDIA_OWNER_IPTV), "iptv") == 0);

    if (g_failures)
    {
        fprintf(stderr, "Player ownership checks failed: %d\n", g_failures);
        return 1;
    }
    puts("Player ownership checks passed");
    return 0;
}
