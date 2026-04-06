#include "player/policy.h"
#include "player/ingress/vendor.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MGTV_FALLBACK_APP_UA "ImgoTV-ipad-AppStore/26031917 CFNetwork/3860.400.51 Darwin/25.3.0"

typedef struct
{
    PlayerMediaVendor vendor;
    const char *referrer;
    const char *origin;
    const char *fallback_user_agent;
    bool prefer_sender_user_agent;
    int timeout_stream_seconds;
    int timeout_file_seconds;
} VendorPolicy;

static bool contains_ignore_case(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return false;

    needle_len = strlen(needle);
    if (needle_len == 0)
        return true;

    for (const char *cursor = haystack; *cursor; ++cursor)
    {
        size_t i = 0;
        while (i < needle_len &&
               cursor[i] &&
               tolower((unsigned char)cursor[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if (i == needle_len)
            return true;
    }

    return false;
}

static bool is_mgtv_sender_ua(const char *user_agent)
{
    return contains_ignore_case(user_agent, "imgotv") ||
           contains_ignore_case(user_agent, "mgintlme") ||
           contains_ignore_case(user_agent, "mgtv");
}

static const VendorPolicy *lookup_vendor_policy(PlayerMediaVendor vendor)
{
    static const VendorPolicy policies[] = {
        {
            PLAYER_MEDIA_VENDOR_BILIBILI,
            "https://www.bilibili.com/",
            "https://www.bilibili.com",
            NULL,
            false,
            15,
            8,
        },
        {
            PLAYER_MEDIA_VENDOR_IQIYI,
            "https://www.iqiyi.com/",
            "https://www.iqiyi.com",
            NULL,
            false,
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_MGTV,
            "https://www.mgtv.com/",
            "https://www.mgtv.com",
            MGTV_FALLBACK_APP_UA,
            true,
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_YOUKU,
            "https://v.youku.com/",
            "https://v.youku.com",
            NULL,
            false,
            18,
            10,
        },
        {
            PLAYER_MEDIA_VENDOR_QQ_VIDEO,
            "https://v.qq.com/",
            "https://v.qq.com",
            NULL,
            false,
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
    media->network_timeout_seconds = (media->format == PLAYER_MEDIA_FORMAT_HLS ||
                                      media->format == PLAYER_MEDIA_FORMAT_FLV ||
                                      media->format == PLAYER_MEDIA_FORMAT_MPEG_TS)
                                         ? policy->timeout_stream_seconds
                                         : policy->timeout_file_seconds;

    snprintf(media->referrer, sizeof(media->referrer), "%s", policy->referrer);
    snprintf(media->origin, sizeof(media->origin), "%s", policy->origin);

    if (policy->prefer_sender_user_agent && is_mgtv_sender_ua(media->sender_user_agent))
        snprintf(media->user_agent, sizeof(media->user_agent), "%s", media->sender_user_agent);
    else if (policy->fallback_user_agent && policy->fallback_user_agent[0] != '\0')
        snprintf(media->user_agent, sizeof(media->user_agent), "%s", policy->fallback_user_agent);

    policy_refresh_header_fields(media);

    if (media->transport == PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY)
    {
        snprintf(media->format_hint,
                 sizeof(media->format_hint),
                 "%s-hls-local-proxy",
                 ingress_vendor_name(media->vendor));
    }
    else
    {
        snprintf(media->format_hint,
                 sizeof(media->format_hint),
                 "%s-%s",
                 ingress_vendor_name(media->vendor),
                 vendor_format_suffix(media->format));
    }
}
