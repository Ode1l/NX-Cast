#include "player/ingress.h"

#include <stdio.h>
#include <string.h>

#include "log/log.h"
#include "player/ingress/classify.h"
#include "player/ingress/evidence.h"
#include "player/ingress/http_probe.h"
#include "player/ingress/model.h"
#include "player/ingress/resource_select.h"
#include "player/ingress/vendor.h"
#include "player/policy.h"

static void clip_for_log(const char *input, char *output, size_t output_size)
{
    size_t length;

    if (!output || output_size == 0)
        return;

    output[0] = '\0';
    if (!input || input[0] == '\0')
        return;

    length = strlen(input);
    if (length + 1 <= output_size)
    {
        memcpy(output, input, length + 1);
        return;
    }

    if (output_size <= 4)
    {
        size_t copy_len = output_size - 1;
        memcpy(output, input, copy_len);
        output[copy_len] = '\0';
        return;
    }

    memcpy(output, input, output_size - 4);
    memcpy(output + output_size - 4, "...", 4);
}

static void log_metadata_summary(const char *uri, const char *metadata, PlayerMediaVendor vendor)
{
    char clipped_uri[256];
    char clipped_metadata[512];

    if (!ingress_vendor_is_sensitive(vendor))
        return;

    clip_for_log(uri, clipped_uri, sizeof(clipped_uri));
    clip_for_log(metadata, clipped_metadata, sizeof(clipped_metadata));

    log_info("[player-ingress] source_detail vendor=%s uri=%s metadata_len=%zu metadata_snippet=%s\n",
             ingress_vendor_name(vendor),
             clipped_uri[0] != '\0' ? clipped_uri : "<empty>",
             metadata ? strlen(metadata) : 0U,
             clipped_metadata[0] != '\0' ? clipped_metadata : "<empty>");
}

static void assign_profile(PlayerMedia *media)
{
    if (!media)
        return;

    if (ingress_vendor_is_sensitive(media->vendor))
        media->profile = PLAYER_MEDIA_PROFILE_VENDOR_SENSITIVE_URL;
    else if (media->format == PLAYER_MEDIA_FORMAT_HLS)
        media->profile = PLAYER_MEDIA_PROFILE_GENERIC_HLS;
    else if (media->flags.is_signed)
        media->profile = PLAYER_MEDIA_PROFILE_SIGNED_EPHEMERAL_URL;
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
    if (!media)
        return;

    assign_profile(media);
    assign_default_hint(media);
    policy_apply_hls(media);
    policy_apply_vendor(media);
    policy_apply_request_context(media, ctx);
    policy_apply_transport(media, ctx);
}

static void apply_http_probe(IngressModel *model, const PlayerOpenContext *ctx)
{
    PlayerMedia probe_media;
    PlayerHttpProbe probe;
    bool likely_segmented;
    PlayerMediaFormat format_before;
    PlayerMediaFormat detected_format = PLAYER_MEDIA_FORMAT_UNKNOWN;
    bool uri_changed = false;
    bool reclassified = false;

    if (!model)
        return;

    ingress_reset(&probe_media);
    ingress_model_apply_to_media(model, &probe_media);
    apply_policy_stack(&probe_media, ctx);

    if (!ingress_http_probe_head(&probe_media, &probe) || !probe.attempted)
        return;

    model->probe_attempted = probe.attempted;
    model->probe_ok = probe.ok;
    model->probe_redirected = probe.redirected;
    model->probe_used_get_fallback = probe.used_get_fallback;
    model->probe_status_code = probe.status_code;
    model->probe_redirect_count = probe.redirect_count;
    model->range_support_known = probe.accept_ranges_known;
    model->range_seekable = probe.accept_ranges_seekable;

    format_before = model->format;
    likely_segmented = model->likely_segmented;

    if (probe.effective_uri[0] != '\0' && strcmp(probe.effective_uri, model->resolved_uri) != 0)
    {
        snprintf(model->resolved_uri, sizeof(model->resolved_uri), "%s", probe.effective_uri);
        uri_changed = true;
    }

    if (probe.content_type[0] != '\0')
    {
        detected_format = ingress_classify_format(model->resolved_uri, probe.content_type, &likely_segmented);
        snprintf(model->mime_type, sizeof(model->mime_type), "%s", probe.content_type);

        if (detected_format != PLAYER_MEDIA_FORMAT_UNKNOWN && detected_format != model->format)
        {
            model->format = detected_format;
            reclassified = true;
        }
    }

    if (uri_changed)
        reclassified = true;

    if (reclassified)
        model->likely_segmented = likely_segmented;

    ingress_model_finalize(model);

    log_info("[player-ingress] url_probe status=%d ok=%d redirects=%d get_fallback=%d accept_ranges=%s content_type=%s format_before=%s format_after=%s uri_changed=%d uri=%s\n",
             probe.status_code,
             probe.ok ? 1 : 0,
             probe.redirect_count,
             probe.used_get_fallback ? 1 : 0,
             !probe.accept_ranges_known ? "unknown" :
             (probe.accept_ranges_seekable ? "bytes" : "none"),
             probe.content_type[0] != '\0' ? probe.content_type : "<none>",
             ingress_format_name(format_before),
             ingress_format_name(model->format),
             uri_changed ? 1 : 0,
             model->resolved_uri);
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
    ingress_model_init(&model, uri, metadata, ctx);
    ingress_select_metadata_resource(&model);
    ingress_model_finalize(&model);
    apply_http_probe(&model, ctx);

    // Phase 2: materialize the normalized model into PlayerMedia, then apply
    // playback policy from the final model.
    ingress_reset(out);
    ingress_model_apply_to_media(&model, out);
    log_metadata_summary(model.input_uri, model.metadata, model.vendor);
    apply_policy_stack(out, ctx);

    return true;
}
