#include "player/ingress/classify.h"

#include <ctype.h>
#include <string.h>

#include "player/ingress/evidence.h"
#include "player/ingress/hls.h"
#include "player/ingress/vendor.h"

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

static bool is_dash_uri(const char *uri)
{
    return contains_ignore_case(uri, ".mpd") || contains_ignore_case(uri, ".m4s");
}

static bool is_flv_uri(const char *uri)
{
    return contains_ignore_case(uri, ".flv");
}

static bool is_mp4_uri(const char *uri)
{
    return contains_ignore_case(uri, ".mp4");
}

static bool is_mpeg_ts_uri(const char *uri)
{
    return contains_ignore_case(uri, ".ts");
}

PlayerMediaFormat ingress_classify_format(const char *uri, const char *metadata_or_mime, bool *likely_segmented)
{
    if (ingress_hls_mime_matches(metadata_or_mime) || ingress_hls_uri_matches(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_HLS;
    }

    if (contains_ignore_case(metadata_or_mime, "application/dash+xml") || is_dash_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = true;
        return PLAYER_MEDIA_FORMAT_DASH;
    }

    if (contains_ignore_case(metadata_or_mime, "video/x-flv") || is_flv_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_FLV;
    }

    if (contains_ignore_case(metadata_or_mime, "video/vnd.dlna.mpeg-tts") ||
        contains_ignore_case(metadata_or_mime, "video/mp2t") ||
        contains_ignore_case(metadata_or_mime, "video/x-mp2t") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=avc_ts") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=mpeg_ts") ||
        is_mpeg_ts_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_MPEG_TS;
    }

    if (contains_ignore_case(metadata_or_mime, "video/mp4") ||
        contains_ignore_case(metadata_or_mime, "application/mp4") ||
        contains_ignore_case(metadata_or_mime, "video/quicktime") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=avc_mp4") ||
        contains_ignore_case(metadata_or_mime, "dlna.org_pn=mpeg4") ||
        is_mp4_uri(uri))
    {
        if (likely_segmented)
            *likely_segmented = false;
        return PLAYER_MEDIA_FORMAT_MP4;
    }

    if (likely_segmented)
        *likely_segmented = false;
    return PLAYER_MEDIA_FORMAT_UNKNOWN;
}

PlayerMediaVendor ingress_classify_vendor(const char *resolved_uri,
                                          const char *original_uri,
                                          const char *metadata,
                                          const IngressEvidence *evidence)
{
    (void)resolved_uri;
    (void)original_uri;
    (void)metadata;
    (void)evidence;
    return PLAYER_MEDIA_VENDOR_UNKNOWN;
}

PlayerMediaTransport ingress_classify_transport(const char *resolved_uri,
                                                PlayerMediaFormat format,
                                                bool likely_segmented,
                                                PlayerMediaVendor vendor)
{
    (void)resolved_uri;
    (void)likely_segmented;
    (void)vendor;

    if (format == PLAYER_MEDIA_FORMAT_HLS)
        return PLAYER_MEDIA_TRANSPORT_HLS_DIRECT;

    if (format == PLAYER_MEDIA_FORMAT_MP4 ||
        format == PLAYER_MEDIA_FORMAT_FLV ||
        format == PLAYER_MEDIA_FORMAT_MPEG_TS ||
        format == PLAYER_MEDIA_FORMAT_DASH)
    {
        return PLAYER_MEDIA_TRANSPORT_HTTP_FILE;
    }

    return PLAYER_MEDIA_TRANSPORT_UNKNOWN;
}

void ingress_apply_classification(PlayerMedia *media, bool likely_segmented, const IngressEvidence *evidence)
{
    if (!media)
        return;

    (void)evidence;

    media->vendor = PLAYER_MEDIA_VENDOR_UNKNOWN;
    media->transport = ingress_classify_transport(media->uri, media->format, likely_segmented, media->vendor);
    media->flags.likely_live = media->format == PLAYER_MEDIA_FORMAT_HLS &&
                               ingress_hls_live_hint(media->uri, media->metadata);
    media->flags.is_signed = false;
    media->flags.likely_segmented = likely_segmented;
    media->flags.likely_video_only = media->format == PLAYER_MEDIA_FORMAT_DASH && likely_segmented;
}
