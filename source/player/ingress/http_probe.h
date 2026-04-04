#pragma once

#include <stdbool.h>

#include "player/ingress.h"

typedef struct
{
    bool attempted;
    bool ok;
    bool redirected;
    bool used_get_fallback;
    int status_code;
    int redirect_count;
    bool accept_ranges_known;
    bool accept_ranges_seekable;
    char content_type[PLAYER_MEDIA_MIME_TYPE_MAX];
    char effective_uri[PLAYER_MEDIA_URI_MAX];
} PlayerHttpProbe;

bool ingress_http_probe_head(const PlayerMedia *media, PlayerHttpProbe *out);
