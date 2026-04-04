#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "player/ingress.h"

typedef struct
{
    const char *input_uri;
    const char *metadata;
    PlayerMediaVendor sender_vendor;
    char metadata_mime[PLAYER_MEDIA_MIME_TYPE_MAX];
    bool input_is_http;
    bool input_is_https;
    bool input_is_signed;
    bool input_is_bare_ipv4;
} IngressEvidence;

bool ingress_evidence_is_http_like_uri(const char *uri);
bool ingress_evidence_is_https_uri(const char *uri);
bool ingress_evidence_uri_host_is_ipv4_literal(const char *uri);
bool ingress_evidence_has_signed_tokens(const char *uri);
void ingress_evidence_detect_metadata_mime(const char *metadata, char *mime_type, size_t mime_type_size);
void ingress_collect_evidence(const char *uri, const char *metadata, const PlayerOpenContext *ctx, IngressEvidence *out);
