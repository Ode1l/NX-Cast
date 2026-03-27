#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Flush queued log messages into in-memory history.
// Call this from the main thread (e.g. once per frame).
void log_flush(void);

// Optional helper for future lifecycle usage.
void log_set_enabled(bool enabled);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);

size_t log_history_count(void);
bool log_history_get_line(size_t index, char *out, size_t out_size);

void vlog_write(LogLevel level, const char *fmt, va_list args);
