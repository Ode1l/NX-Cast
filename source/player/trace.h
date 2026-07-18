#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t player_trace_begin_media(const char *reason, const char *uri, const char *metadata);
uint32_t player_trace_current_media_seq(void);
uint32_t player_trace_current_media_hash(void);
uint64_t player_trace_elapsed_ms(void);
uint32_t player_trace_uri_hash(const char *uri);
const char *player_trace_uri_summary(const char *uri, char *buffer, size_t buffer_size);
void player_trace_log(const char *fmt, ...);
void player_trace_warn(const char *fmt, ...);
