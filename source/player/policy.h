#pragma once

#include "ingress.h"

void policy_apply_default(PlayerMedia *media);
void policy_apply_hls(PlayerMedia *media);
void policy_apply_vendor(PlayerMedia *media);
void policy_apply_request_context(PlayerMedia *media, const PlayerOpenContext *ctx);
void policy_apply_transport(PlayerMedia *media, const PlayerOpenContext *ctx);
void policy_refresh_header_fields(PlayerMedia *media);
