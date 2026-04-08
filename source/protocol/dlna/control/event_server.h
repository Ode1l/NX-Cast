#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "player/renderer.h"
#include "protocol/http/http_server.h"

bool event_server_start(void);
void event_server_stop(void);

bool event_server_try_handle_http(const HttpRequestContext *ctx,
                                  char *response,
                                  size_t response_size,
                                  size_t *response_len);

void event_server_on_renderer_event(const RendererEvent *event);
