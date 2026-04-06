#pragma once

#include "ingress.h"

// Policy is intentionally the last stage in ingress resolution. It consumes a
// fully classified PlayerMedia and may only refine playback configuration
// (headers, timeouts, load options). It must not re-parse URI/metadata or
// re-decide format/vendor/transport.
void policy_apply_default(PlayerMedia *media);
void policy_apply_hls(PlayerMedia *media);
void policy_apply_vendor(PlayerMedia *media);
void policy_apply_request_context(PlayerMedia *media, const PlayerOpenContext *ctx);
void policy_apply_transport(PlayerMedia *media, const PlayerOpenContext *ctx);
void policy_refresh_header_fields(PlayerMedia *media);
