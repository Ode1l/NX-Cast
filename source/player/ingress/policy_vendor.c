#include "player/policy.h"
#include "player/ingress/vendor.h"

#include <stddef.h>
#include <stdio.h>

typedef struct
{
    PlayerMediaVendor vendor;
    const char *referrer;
    const char *origin;
    int timeout_stream_seconds;
    int timeout_file_seconds;
} VendorPolicy;

static const VendorPolicy *lookup_vendor_policy(PlayerMediaVendor vendor)
{
    static const VendorPolicy policies[] = {
        {
            PLAYER_MEDIA_VENDOR_BILIBILI,
            "https://www.bilibili.com/",
            "https://www.bilibili.com",
            15,
            8,
        },
        {
            PLAYER_MEDIA_VENDOR_IQIYI,
            "https://www.iqiyi.com/",
            "https://www.iqiyi.com",
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_MGTV,
            "https://www.mgtv.com/",
            "https://www.mgtv.com",
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_YOUKU,
            "https://v.youku.com/",
            "https://v.youku.com",
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_QQ_VIDEO,
            "https://v.qq.com/",
            "https://v.qq.com",
            18,
            10,
        },
    };

    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i)
    {
        if (policies[i].vendor == vendor)
            return &policies[i];
    }

    return NULL;
}

static const char *vendor_format_suffix(PlayerMediaFormat format)
{
    switch (format)
    {
    case PLAYER_MEDIA_FORMAT_HLS:
        return "hls";
    case PLAYER_MEDIA_FORMAT_DASH:
        return "dash";
    case PLAYER_MEDIA_FORMAT_FLV:
        return "flv";
    case PLAYER_MEDIA_FORMAT_MP4:
        return "mp4";
    case PLAYER_MEDIA_FORMAT_MPEG_TS:
        return "mpeg-ts";
    case PLAYER_MEDIA_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

void policy_apply_vendor(PlayerMedia *media)
{
    const VendorPolicy *policy;

    if (!media)
        return;

    policy = lookup_vendor_policy(media->vendor);
    if (!policy || !ingress_vendor_is_sensitive(media->vendor))
        return;

    media->profile = PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL;
    media->network_timeout_seconds = (media->flags.is_hls || media->flags.is_flv || media->flags.is_mpeg_ts)
                                         ? policy->timeout_stream_seconds
                                         : policy->timeout_file_seconds;

    snprintf(media->referrer, sizeof(media->referrer), "%s", policy->referrer);
    snprintf(media->origin, sizeof(media->origin), "%s", policy->origin);
    snprintf(media->header_fields,
             sizeof(media->header_fields),
             "Referer: %s,Origin: %s,User-Agent: %s",
             policy->referrer,
             policy->origin,
             media->user_agent);

    snprintf(media->format_hint,
             sizeof(media->format_hint),
             "%s-%s",
             ingress_vendor_name(media->vendor),
             vendor_format_suffix(media->format));
}
