#include "player/ingress/vendor.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

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

static bool matches_any(const char *uri, const char *metadata,
                        const char *const *needles, size_t needle_count)
{
    for (size_t i = 0; i < needle_count; ++i)
    {
        if (contains_ignore_case(uri, needles[i]) || contains_ignore_case(metadata, needles[i]))
            return true;
    }

    return false;
}

PlayerMediaVendor ingress_detect_vendor(const char *uri, const char *metadata)
{
    static const char *const bilibili_needles[] = {
        "bilibili.com",
        "bilivideo.com",
        "hdslb.com",
        "bililive",
        "upgcxcode",
        "upos",
        "bvc-vod"
    };
    static const char *const iqiyi_needles[] = {
        "iqiyi.com",
        "qiyi.com",
        "iq.com",
        "pps.tv"
    };
    static const char *const mgtv_needles[] = {
        "mgtv.com",
        "hunantv.com",
        "hifuntv.com"
    };
    static const char *const youku_needles[] = {
        "youku.com",
        "ykimg.com"
    };
    static const char *const qq_video_needles[] = {
        "v.qq.com",
        "video.qq.com",
        "qqvideo.tc.qq.com",
        "video.dispatch.tc.qq.com",
        "dispatch.tc.qq.com"
    };
    static const char *const cctv_needles[] = {
        "cctv",
        "cntv.cn",
        "yangshipin.cn"
    };

    if (matches_any(uri, metadata, bilibili_needles, sizeof(bilibili_needles) / sizeof(bilibili_needles[0])))
        return PLAYER_MEDIA_VENDOR_BILIBILI;
    if (matches_any(uri, metadata, iqiyi_needles, sizeof(iqiyi_needles) / sizeof(iqiyi_needles[0])))
        return PLAYER_MEDIA_VENDOR_IQIYI;
    if (matches_any(uri, metadata, mgtv_needles, sizeof(mgtv_needles) / sizeof(mgtv_needles[0])))
        return PLAYER_MEDIA_VENDOR_MGTV;
    if (matches_any(uri, metadata, youku_needles, sizeof(youku_needles) / sizeof(youku_needles[0])))
        return PLAYER_MEDIA_VENDOR_YOUKU;
    if (matches_any(uri, metadata, qq_video_needles, sizeof(qq_video_needles) / sizeof(qq_video_needles[0])))
        return PLAYER_MEDIA_VENDOR_QQ_VIDEO;
    if (matches_any(uri, metadata, cctv_needles, sizeof(cctv_needles) / sizeof(cctv_needles[0])))
        return PLAYER_MEDIA_VENDOR_CCTV;

    return PLAYER_MEDIA_VENDOR_UNKNOWN;
}

PlayerMediaVendor ingress_detect_vendor_from_sender_ua(const char *sender_user_agent)
{
    static const char *const bilibili_needles[] = {
        "bili",
        "bilicast",
        "bilidlna"
    };
    static const char *const iqiyi_needles[] = {
        "iqiyidlna",
        "iqiyi"
    };
    static const char *const mgtv_needles[] = {
        "imgotv",
        "mgintlme",
        "mgtv",
        "hunantv"
    };
    static const char *const youku_needles[] = {
        "youku"
    };
    static const char *const qq_video_needles[] = {
        "qqvideo",
        "qqlive",
        "tencentvideo",
        "live4iphonerel",
        "live4iphone"
    };
    static const char *const cctv_needles[] = {
        "yangshipin",
        "cctv"
    };

    if (!sender_user_agent || sender_user_agent[0] == '\0')
        return PLAYER_MEDIA_VENDOR_UNKNOWN;

    if (matches_any(sender_user_agent, NULL, bilibili_needles, sizeof(bilibili_needles) / sizeof(bilibili_needles[0])))
        return PLAYER_MEDIA_VENDOR_BILIBILI;
    if (matches_any(sender_user_agent, NULL, iqiyi_needles, sizeof(iqiyi_needles) / sizeof(iqiyi_needles[0])))
        return PLAYER_MEDIA_VENDOR_IQIYI;
    if (matches_any(sender_user_agent, NULL, mgtv_needles, sizeof(mgtv_needles) / sizeof(mgtv_needles[0])))
        return PLAYER_MEDIA_VENDOR_MGTV;
    if (matches_any(sender_user_agent, NULL, youku_needles, sizeof(youku_needles) / sizeof(youku_needles[0])))
        return PLAYER_MEDIA_VENDOR_YOUKU;
    if (matches_any(sender_user_agent, NULL, qq_video_needles, sizeof(qq_video_needles) / sizeof(qq_video_needles[0])))
        return PLAYER_MEDIA_VENDOR_QQ_VIDEO;
    if (matches_any(sender_user_agent, NULL, cctv_needles, sizeof(cctv_needles) / sizeof(cctv_needles[0])))
        return PLAYER_MEDIA_VENDOR_CCTV;

    return PLAYER_MEDIA_VENDOR_UNKNOWN;
}

bool ingress_vendor_is_sensitive(PlayerMediaVendor vendor)
{
    switch (vendor)
    {
    case PLAYER_MEDIA_VENDOR_BILIBILI:
    case PLAYER_MEDIA_VENDOR_IQIYI:
    case PLAYER_MEDIA_VENDOR_MGTV:
    case PLAYER_MEDIA_VENDOR_YOUKU:
    case PLAYER_MEDIA_VENDOR_QQ_VIDEO:
        return true;
    case PLAYER_MEDIA_VENDOR_CCTV:
    case PLAYER_MEDIA_VENDOR_UNKNOWN:
    default:
        return false;
    }
}
