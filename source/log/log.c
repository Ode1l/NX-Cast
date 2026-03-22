#include "log.h"

#include <switch.h>

#include <stdarg.h>
#include <stdio.h>

static Mutex g_logMutex;

__attribute__((constructor)) static void log_mutex_init(void)
{
    mutexInit(&g_logMutex);
}

static const char *level_label(LogLevel level)
{
    switch (level)
    {
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_WARN:
            return "WARN";
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_DEBUG:
        default:
            return "DEBUG";
    }
}

void vlog_write(LogLevel level, const char *fmt, va_list args)
{
    mutexLock(&g_logMutex);
    printf("[%s] ", level_label(level));
    vprintf(fmt, args);
    mutexUnlock(&g_logMutex);
}

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog_write(LOG_LEVEL_DEBUG, fmt, args);
    va_end(args);
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog_write(LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog_write(LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog_write(LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}
