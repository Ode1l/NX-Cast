#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "player/backend/libmpv_airplay.h"
#include "player/cache_policy.h"

static void test_network_policy(void)
{
    PlayerCachePolicy policy =
        player_cache_policy_for_uri("https://example.invalid/live/index.m3u8");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(policy.cache_enabled);
    assert(policy.forward_mib == 20u);
    assert(policy.backward_mib == 10u);
    assert(policy.readahead_secs == 20u);
    assert(strcmp(player_cache_policy_name(policy.kind), "network-buffered") == 0);
    assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
    assert(strcmp(options,
                  "pause=no,cache=yes,cache-pause-initial=no,"
                  "demuxer-readahead-secs=20,demuxer-max-bytes=20MiB,"
                  "demuxer-max-back-bytes=10MiB") == 0);
}

static void test_mirror_policy(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(PLAYER_LIBMPV_AIRPLAY_URI);
    char options[64];

    assert(policy.kind == PLAYER_CACHE_POLICY_AIRPLAY_MIRROR);
    assert(!policy.cache_enabled);
    assert(policy.forward_mib == 0u);
    assert(policy.backward_mib == 0u);
    assert(policy.readahead_secs == 0u);
    assert(strcmp(player_cache_policy_name(policy.kind),
                  "airplay-mirror-low-latency") == 0);
    assert(player_cache_policy_format_options(&policy, true, options, sizeof(options)));
    assert(strcmp(options, "pause=yes,cache=no") == 0);
}

static void test_unknown_uri_and_invalid_output(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(NULL);
    char small[8];

    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!player_cache_policy_format_options(&policy, false, small, sizeof(small)));
    assert(small[sizeof(small) - 1] == '\0');
    assert(!player_cache_policy_format_options(NULL, false, small, sizeof(small)));
    assert(!player_cache_policy_format_options(&policy, false, NULL, 0));
}

int main(void)
{
    test_network_policy();
    test_mirror_policy();
    test_unknown_uri_and_invalid_output();
    puts("player cache policy tests passed");
    return 0;
}
