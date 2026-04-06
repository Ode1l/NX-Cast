#pragma once

#include "player/ingress.h"
#include "player/ingress/evidence.h"

// IngressModel is the normalized result of parsing standard DLNA inputs
// (CurrentURI, CurrentURIMetaData, request headers) before any playback policy
// is applied. It answers "what is this source?" without yet deciding
// "how should mpv open it?".
typedef struct
{
    IngressEvidence evidence;
    char input_uri[PLAYER_MEDIA_URI_MAX];
    char metadata[PLAYER_MEDIA_METADATA_MAX];
    char resolved_uri[PLAYER_MEDIA_URI_MAX];
    char protocol_info[PLAYER_MEDIA_PROTOCOL_INFO_MAX];
    char mime_type[PLAYER_MEDIA_MIME_TYPE_MAX];
    PlayerMediaFormat format;
    PlayerMediaTransport transport;
    PlayerMediaVendor detected_vendor;
    PlayerMediaVendor hint_vendor;
    PlayerMediaVendor vendor;
    bool likely_segmented;
    bool selected_from_metadata;
    int metadata_candidate_count;
    bool probe_attempted;
    bool probe_ok;
    bool probe_redirected;
    bool probe_used_get_fallback;
    int probe_status_code;
    int probe_redirect_count;
    bool range_support_known;
    bool range_seekable;
} IngressModel;

void ingress_model_init(IngressModel *model, const char *uri, const char *metadata, const PlayerOpenContext *ctx);
void ingress_model_finalize(IngressModel *model);
void ingress_model_apply_to_media(const IngressModel *model, PlayerMedia *media);
