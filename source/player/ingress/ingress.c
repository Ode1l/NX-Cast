#include "player/ingress.h"

#include <stdio.h>
#include <string.h>

#include "log/log.h"
#include "player/ingress/classify.h"
#include "player/ingress/evidence.h"
#include "player/ingress/model.h"
#include "player/ingress/resource_select.h"
#include "player/policy.h"

static void assign_profile(PlayerMedia *media)
{
    if (!media)
        return;

    if (media->format == PLAYER_MEDIA_FORMAT_HLS)
        media->profile = PLAYER_MEDIA_PROFILE_GENERIC_HLS;
    else if (media->transport == PLAYER_MEDIA_TRANSPORT_HTTP_FILE)
        media->profile = PLAYER_MEDIA_PROFILE_DIRECT_HTTP_FILE;
    else
        media->profile = PLAYER_MEDIA_PROFILE_UNKNOWN;
}

static void assign_default_hint(PlayerMedia *media)
{
    if (!media)
        return;

    snprintf(media->format_hint, sizeof(media->format_hint), "%s", ingress_format_name(media->format));
}

static void apply_policy_stack(PlayerMedia *media, const PlayerOpenContext *ctx)
{
    (void)ctx;

    if (!media)
        return;

    assign_profile(media);
    assign_default_hint(media);
    policy_apply_default(media);
}

void ingress_reset(PlayerMedia *media)
{
    if (!media)
        return;

    memset(media, 0, sizeof(*media));
    media->profile = PLAYER_MEDIA_PROFILE_UNKNOWN;
    media->transport = PLAYER_MEDIA_TRANSPORT_UNKNOWN;
    policy_apply_default(media);
}

const char *ingress_profile_name(PlayerMediaProfile profile)
{
    switch (profile)
    {
    case PLAYER_MEDIA_PROFILE_DIRECT_HTTP_FILE:
        return "direct-http-file";
    case PLAYER_MEDIA_PROFILE_GENERIC_HLS:
        return "generic-hls";
    case PLAYER_MEDIA_PROFILE_HEADER_SENSITIVE_HTTP:
        return "header-sensitive-http";
    case PLAYER_MEDIA_PROFILE_SIGNED_EPHEMERAL_URL:
        return "signed-ephemeral-url";
    case PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL:
        return "vendor-sensitive-url";
    case PLAYER_MEDIA_PROFILE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ingress_vendor_name(PlayerMediaVendor vendor)
{
    switch (vendor)
    {
    case PLAYER_MEDIA_VENDOR_BILIBILI:
        return "bilibili";
    case PLAYER_MEDIA_VENDOR_IQIYI:
        return "iqiyi";
    case PLAYER_MEDIA_VENDOR_MGTV:
        return "mgtv";
    case PLAYER_MEDIA_VENDOR_YOUKU:
        return "youku";
    case PLAYER_MEDIA_VENDOR_QQ_VIDEO:
        return "qq-video";
    case PLAYER_MEDIA_VENDOR_CCTV:
        return "cctv";
    case PLAYER_MEDIA_VENDOR_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ingress_format_name(PlayerMediaFormat format)
{
    switch (format)
    {
    case PLAYER_MEDIA_FORMAT_MP4:
        return "mp4";
    case PLAYER_MEDIA_FORMAT_FLV:
        return "flv";
    case PLAYER_MEDIA_FORMAT_HLS:
        return "hls";
    case PLAYER_MEDIA_FORMAT_DASH:
        return "dash";
    case PLAYER_MEDIA_FORMAT_MPEG_TS:
        return "mpeg-ts";
    case PLAYER_MEDIA_FORMAT_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ingress_transport_name(PlayerMediaTransport transport)
{
    switch (transport)
    {
    case PLAYER_MEDIA_TRANSPORT_HTTP_FILE:
        return "http-file";
    case PLAYER_MEDIA_TRANSPORT_HLS_DIRECT:
        return "hls-direct";
    case PLAYER_MEDIA_TRANSPORT_HLS_LOCAL_PROXY:
        return "hls-local-proxy";
    case PLAYER_MEDIA_TRANSPORT_HLS_GATEWAY:
        return "hls-gateway";
    case PLAYER_MEDIA_TRANSPORT_UNKNOWN:
    default:
        return "unknown";
    }
}

bool ingress_resolve(const char *uri, const char *metadata, PlayerMedia *out)
{
    return ingress_resolve_with_context(uri, metadata, NULL, out);
}

bool ingress_resolve_with_context(const char *uri, const char *metadata, const PlayerOpenContext *ctx, PlayerMedia *out)
{
    IngressModel model;

    if (!uri || uri[0] == '\0' || !out)
        return false;

    // Phase 1: normalize standard DLNA inputs into a stable model.
    ingress_model_init(&model, uri, metadata, NULL);
    ingress_select_metadata_resource(&model);
    ingress_model_finalize(&model);

    ingress_reset(out);
    ingress_model_apply_to_media(&model, out);
    apply_policy_stack(out, NULL);

    return true;
}
