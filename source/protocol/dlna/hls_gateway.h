#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "protocol/http/http_server.h"

bool hls_gateway_start(const char *url_base);
void hls_gateway_stop(void);
bool hls_gateway_prepare_media_uri(const char *source_uri, char **playback_uri_out);
bool hls_gateway_try_handle_http(const HttpRequestContext *ctx,
                                 char *response,
                                 size_t response_size,
                                 size_t *response_len);
