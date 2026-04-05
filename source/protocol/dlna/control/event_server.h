#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "player/types.h"
#include "protocol/http/http_server.h"

bool event_server_start(void);
void event_server_stop(void);

bool event_server_try_handle_http(const HttpRequestContext *ctx,
                                  char *response,
                                  size_t response_size,
                                  size_t *response_len);

// GENA consumes the same player event stream as SOAP runtime state. It should
// stay a thin protocol layer, not a second source of truth.
void event_server_on_player_event(const PlayerEvent *event);
