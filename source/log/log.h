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

bool log_runtime_init(void);
void log_runtime_shutdown(void);

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Compatibility hook. Logging is now flushed by a dedicated worker thread.
void log_flush(void);

void log_set_enabled(bool enabled);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
void log_set_stdio_mirror(bool enabled);

size_t log_history_count(void);
bool log_history_get_line(size_t index, char *out, size_t out_size);

void vlog_write(LogLevel level, const char *fmt, va_list args);
