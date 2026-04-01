#include "source_policy.h"

#include <stdio.h>

#define SOURCE_POLICY_BILIBILI_REFERRER "https://www.bilibili.com/"
#define SOURCE_POLICY_BILIBILI_ORIGIN "https://www.bilibili.com"

void source_policy_apply_vendor(PlayerResolvedSource *source)
{
    if (!source || !source->flags.is_bilibili)
        return;

    source->profile = PLAYER_SOURCE_PROFILE_VENDOR_SENSITIVE_URL;
    source->network_timeout_seconds = source->flags.is_hls ? 15 : 8;
    snprintf(source->referrer, sizeof(source->referrer), "%s", SOURCE_POLICY_BILIBILI_REFERRER);
    snprintf(source->origin, sizeof(source->origin), "%s", SOURCE_POLICY_BILIBILI_ORIGIN);
    snprintf(source->header_fields,
             sizeof(source->header_fields),
             "Referer: %s,Origin: %s,User-Agent: %s",
             SOURCE_POLICY_BILIBILI_REFERRER,
             SOURCE_POLICY_BILIBILI_ORIGIN,
             source->user_agent);

    if (source->flags.is_dash)
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-dash");
    else if (source->flags.is_hls)
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-hls");
    else if (source->flags.is_flv)
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-flv");
    else if (source->flags.is_mp4)
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-mp4");
    else if (source->flags.is_mpeg_ts)
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-mpeg-ts");
    else
        snprintf(source->format_hint, sizeof(source->format_hint), "%s", "bilibili-unknown");
}
