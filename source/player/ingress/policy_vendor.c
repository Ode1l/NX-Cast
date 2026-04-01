#include "player/policy.h"

#include <stdio.h>

#define SOURCE_POLICY_BILIBILI_REFERRER "https://www.bilibili.com/"
#define SOURCE_POLICY_BILIBILI_ORIGIN "https://www.bilibili.com"

void policy_apply_vendor(PlayerMedia *media)
{
    if (!media || !media->flags.is_bilibili)
        return;

    media->profile = PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL;
    media->network_timeout_seconds = media->flags.is_hls ? 15 : 8;
    snprintf(media->referrer, sizeof(media->referrer), "%s", SOURCE_POLICY_BILIBILI_REFERRER);
    snprintf(media->origin, sizeof(media->origin), "%s", SOURCE_POLICY_BILIBILI_ORIGIN);
    snprintf(media->header_fields,
             sizeof(media->header_fields),
             "Referer: %s,Origin: %s,User-Agent: %s",
             SOURCE_POLICY_BILIBILI_REFERRER,
             SOURCE_POLICY_BILIBILI_ORIGIN,
             media->user_agent);

    if (media->flags.is_dash)
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-dash");
    else if (media->flags.is_hls)
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-hls");
    else if (media->flags.is_flv)
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-flv");
    else if (media->flags.is_mp4)
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-mp4");
    else if (media->flags.is_mpeg_ts)
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-mpeg-ts");
    else
        snprintf(media->format_hint, sizeof(media->format_hint), "%s", "bilibili-unknown");
}
