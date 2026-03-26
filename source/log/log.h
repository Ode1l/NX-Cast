#pragma once

#include <stdarg.h>
#include <stdbool.h>

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

// Flush queued log messages to the console.
// Call this from the main thread (e.g. once per frame).
void log_flush(void);

// Optional helper for future lifecycle usage.
void log_set_enabled(bool enabled);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
void log_set_verbose_payload(bool enabled);
bool log_get_verbose_payload(void);

void vlog_write(LogLevel level, const char *fmt, va_list args);
