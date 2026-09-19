#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "player/backend/libmpv_airplay.h"
#include "player/cache_policy.h"

static void test_live_hls_uses_player_defaults(void)
{
    PlayerCachePolicy policy =
        player_cache_policy_for_uri("https://example.invalid/live/index.m3u8");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(policy.cache_enabled);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(policy.forward_mib == 0u);
    assert(policy.backward_mib == 0u);
    assert(policy.readahead_secs == 0u);
    assert(strcmp(player_cache_policy_name(policy.kind), "network-default") == 0);
    assert(player_cache_policy_format_options(&policy, false, options, sizeof(options)));
    assert(strcmp(options, "pause=no") == 0);
}

static void test_direct_mp4_uses_small_startup_cache(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(
        "https://example.invalid/video/clip.MP4?token=1");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_DIRECT_MP4);
    assert(policy.configure_cache);
    assert(policy.cache_enabled);
    assert(policy.forward_mib == 8u);
    assert(policy.backward_mib == 2u);
    assert(policy.readahead_secs == 2u);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strcmp(options,
                  "pause=no,cache=yes,cache-pause-initial=no,"
                  "demuxer-readahead-secs=2,demuxer-max-bytes=8MiB,"
                  "demuxer-max-back-bytes=2MiB") == 0);
}

static void test_airplay_remote_hls_policy(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(
        "http://127.0.0.1:7000/airplay-hls/session/master.m3u8");
    char options[256];

    assert(policy.kind == PLAYER_CACHE_POLICY_AIRPLAY_REMOTE_HLS);
    assert(policy.configure_cache);
    assert(policy.cache_enabled);
    assert(policy.disable_http_persistence);
    assert(strcmp(player_cache_policy_name(policy.kind),
                  "airplay-reverse-hls") == 0);
    assert(player_cache_policy_format_options(&policy, true, options,
                                              sizeof(options)));
    assert(strcmp(options,
                  "pause=yes,cache=yes,cache-pause-initial=no,"
                  "demuxer-readahead-secs=20,demuxer-max-bytes=20MiB,"
                  "demuxer-max-back-bytes=10MiB,"
                  "demuxer-lavf-o=http_persistent=no") == 0);

    policy = player_cache_policy_for_uri(
        "https://example.invalid/airplay-hls/session/master.m3u8");
    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strstr(options, "demuxer-lavf-format") == NULL);
    policy = player_cache_policy_for_uri(
        "http://127.0.0.1:7000/channels/live.m3u8");
    assert(policy.kind == PLAYER_CACHE_POLICY_NETWORK);
    assert(!policy.configure_cache);
    assert(!policy.disable_http_persistence);
    assert(player_cache_policy_format_options(&policy, false, options,
                                              sizeof(options)));
    assert(strstr(options, "demuxer-lavf-format") == NULL);
}

static void test_mirror_policy(void)
{
    PlayerCachePolicy policy = player_cache_policy_for_uri(PLAYER_LIBMPV_AIRPLAY_URI);
    char options[64];

    assert(policy.kind == PLAYER_CACHE_POLICY_AIRPLAY_MIRROR);
    assert(policy.configure_cache);
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
    test_live_hls_uses_player_defaults();
    test_direct_mp4_uses_small_startup_cache();
    test_airplay_remote_hls_policy();
    test_mirror_policy();
    test_unknown_uri_and_invalid_output();
    puts("player cache policy tests passed");
    return 0;
}
