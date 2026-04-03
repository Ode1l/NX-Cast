#pragma once

#include <stdbool.h>

bool ingress_hls_uri_matches(const char *uri);
bool ingress_hls_mime_matches(const char *value);
bool ingress_hls_live_hint(const char *uri, const char *metadata);
int ingress_hls_default_readahead_seconds(bool live_hint);
int ingress_hls_cache_pause_wait_seconds(bool live_hint);
