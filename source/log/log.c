#include "log.h"

#include <switch.h>

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static Mutex g_logMutex;
static bool g_logEnabled = true;
static LogLevel g_logMinLevel = LOG_LEVEL_INFO;
static bool g_logVerbosePayload = false;

#define LOG_QUEUE_CAPACITY 512
#define LOG_MESSAGE_MAX 1024

typedef struct
{
    LogLevel level;
    char text[LOG_MESSAGE_MAX];
} LogEntry;

static LogEntry g_logQueue[LOG_QUEUE_CAPACITY];
static size_t g_logHead = 0;
static size_t g_logTail = 0;
static size_t g_logCount = 0;
static unsigned int g_logDroppedCount = 0;

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
    if (!fmt || !g_logEnabled)
        return;
    if (level < g_logMinLevel)
        return;

    char formatted[LOG_MESSAGE_MAX];
    int written = vsnprintf(formatted, sizeof(formatted), fmt, args);
    if (written < 0)
        return;

    mutexLock(&g_logMutex);

    // Queue logs from all threads; only main thread should print to console.
    if (g_logCount >= LOG_QUEUE_CAPACITY)
    {
        g_logDroppedCount++;
        mutexUnlock(&g_logMutex);
        return;
    }

    LogEntry *entry = &g_logQueue[g_logTail];
    entry->level = level;
    snprintf(entry->text, sizeof(entry->text), "%s", formatted);

    g_logTail = (g_logTail + 1) % LOG_QUEUE_CAPACITY;
    g_logCount++;
    mutexUnlock(&g_logMutex);
}

void log_flush(void)
{
    if (!g_logEnabled)
        return;

    while (true)
    {
        LogEntry entry;
        bool has_entry = false;
        unsigned int dropped = 0;

        mutexLock(&g_logMutex);
        if (g_logCount > 0)
        {
            entry = g_logQueue[g_logHead];
            g_logHead = (g_logHead + 1) % LOG_QUEUE_CAPACITY;
            g_logCount--;
            has_entry = true;
        }
        else if (g_logDroppedCount > 0)
        {
            dropped = g_logDroppedCount;
            g_logDroppedCount = 0;
        }
        mutexUnlock(&g_logMutex);

        if (has_entry)
        {
            printf("[%s] %s", level_label(entry.level), entry.text);
            size_t len = strlen(entry.text);
            if (len == 0 || entry.text[len - 1] != '\n')
                printf("\n");
            continue;
        }

        if (dropped > 0)
        {
            printf("[WARN] log queue full, dropped %u messages\n", dropped);
            continue;
        }

        break;
    }
}

void log_set_enabled(bool enabled)
{
    mutexLock(&g_logMutex);
    g_logEnabled = enabled;
    mutexUnlock(&g_logMutex);
}

void log_set_level(LogLevel level)
{
    if (level < LOG_LEVEL_DEBUG)
        level = LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_ERROR)
        level = LOG_LEVEL_ERROR;

    mutexLock(&g_logMutex);
    g_logMinLevel = level;
    mutexUnlock(&g_logMutex);
}

LogLevel log_get_level(void)
{
    mutexLock(&g_logMutex);
    LogLevel level = g_logMinLevel;
    mutexUnlock(&g_logMutex);
    return level;
}

void log_set_verbose_payload(bool enabled)
{
    mutexLock(&g_logMutex);
    g_logVerbosePayload = enabled;
    mutexUnlock(&g_logMutex);
}

bool log_get_verbose_payload(void)
{
    mutexLock(&g_logMutex);
    bool enabled = g_logVerbosePayload;
    mutexUnlock(&g_logMutex);
    return enabled;
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
