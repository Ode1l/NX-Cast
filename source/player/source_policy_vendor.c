#include "source_policy.h"

#include <stdio.h>

#define SOURCE_POLICY_BILIBILI_REFERRER "https://www.bilibili.com/"
#define SOURCE_POLICY_BILIBILI_ORIGIN "https://www.bilibili.com"

void source_policy_apply_vendor(PlayerResolvedSource *source)
{
    if (!source || !source->flags.is_bilibili)
        return;

    source->profile = PLAYER_SOURCE_PROFILE_VENDOR_SENSITIVE_URL;
    source->network_timeout_seconds = 5;
    snprintf(source->referrer, sizeof(source->referrer), "%s", SOURCE_POLICY_BILIBILI_REFERRER);
    snprintf(source->origin, sizeof(source->origin), "%s", SOURCE_POLICY_BILIBILI_ORIGIN);
    snprintf(source->header_fields,
             sizeof(source->header_fields),
             "Referer: %s,Origin: %s,User-Agent: %s",
             SOURCE_POLICY_BILIBILI_REFERRER,
             SOURCE_POLICY_BILIBILI_ORIGIN,
             source->user_agent);
}
