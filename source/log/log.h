#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

typedef struct
{
    uint64_t enqueued;
    uint64_t processed;
    uint64_t queue_dropped;
    uint64_t mirror_dropped;
    uint64_t mirror_failures;
    size_t queue_depth;
    size_t queue_high_watermark;
    uint64_t worker_heartbeat_age_ms;
    bool worker_running;
    bool worker_waiting;
    bool socket_mirror_enabled;
} LogRuntimeStats;

#ifndef NXCAST_LOG_LEVEL_DEFAULT
#if defined(NXCAST_TRACE_BUILD) && NXCAST_TRACE_BUILD
#define NXCAST_LOG_LEVEL_DEFAULT LOG_LEVEL_INFO
#else
#define NXCAST_LOG_LEVEL_DEFAULT LOG_LEVEL_WARN
#endif
#endif

bool log_runtime_init(void);
void log_runtime_shutdown(void);
void log_set_socket_mirror(int socket_fd);
bool log_get_runtime_stats(LogRuntimeStats *stats_out);

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Compatibility hook. Logging is now flushed by a dedicated worker thread.
void log_flush(void);

void log_set_enabled(bool enabled);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
const char *log_get_mpv_level(void);
void log_set_stdio_mirror(bool enabled);

size_t log_history_count(void);
bool log_history_get_line(size_t index, char *out, size_t out_size);

void vlog_write(LogLevel level, const char *fmt, va_list args);
