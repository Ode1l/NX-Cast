#include "player/policy.h"

#include <stdio.h>

#define SOURCE_POLICY_DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"

void policy_apply_default(PlayerMedia *media)
{
    if (!media)
        return;

    media->network_timeout_seconds = 10;
    media->demuxer_readahead_seconds = 8;
    snprintf(media->user_agent, sizeof(media->user_agent), "%s", SOURCE_POLICY_DEFAULT_USER_AGENT);
    snprintf(media->probe_info, sizeof(media->probe_info), "%s", "auto");
}
