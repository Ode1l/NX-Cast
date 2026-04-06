#pragma once

#include <stdbool.h>

#include "player/ingress.h"
#include "player/ingress/evidence.h"

PlayerMediaFormat ingress_classify_format(const char *uri, const char *metadata_or_mime, bool *likely_segmented);
PlayerMediaVendor ingress_classify_vendor(const char *resolved_uri,
                                          const char *original_uri,
                                          const char *metadata,
                                          const IngressEvidence *evidence);
PlayerMediaTransport ingress_classify_transport(const char *resolved_uri,
                                                PlayerMediaFormat format,
                                                bool likely_segmented,
                                                PlayerMediaVendor vendor);
void ingress_apply_classification(PlayerMedia *media, bool likely_segmented, const IngressEvidence *evidence);
