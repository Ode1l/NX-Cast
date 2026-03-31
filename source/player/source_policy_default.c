#include "source_policy.h"

#include <stdio.h>

#define SOURCE_POLICY_DEFAULT_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"

void source_policy_apply_default(PlayerResolvedSource *source)
{
    if (!source)
        return;

    source->network_timeout_seconds = 10;
    snprintf(source->user_agent, sizeof(source->user_agent), "%s", SOURCE_POLICY_DEFAULT_USER_AGENT);
    snprintf(source->probe_info, sizeof(source->probe_info), "%s", "auto");
}
