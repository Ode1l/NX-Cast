#include "log.h"

#include <switch.h>

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static Mutex g_logMutex;
static bool g_logEnabled = true;
static LogLevel g_logMinLevel = LOG_LEVEL_INFO;
static bool g_logMirrorToStdio = false;

#define LOG_QUEUE_CAPACITY 512
#define LOG_MESSAGE_MAX 1024
#define LOG_HISTORY_CAPACITY 2048
#define LOG_HISTORY_MESSAGE_MAX 512

typedef struct
{
    LogLevel level;
    char text[LOG_MESSAGE_MAX];
} LogEntry;

typedef struct
{
    char text[LOG_HISTORY_MESSAGE_MAX];
} LogHistoryEntry;

static LogEntry g_logQueue[LOG_QUEUE_CAPACITY];
static size_t g_logHead = 0;
static size_t g_logTail = 0;
static size_t g_logCount = 0;
static unsigned int g_logDroppedCount = 0;

static LogHistoryEntry g_logHistory[LOG_HISTORY_CAPACITY];
static size_t g_logHistoryHead = 0;
static size_t g_logHistoryCount = 0;

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

static void normalize_log_text(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    for (; src[i] && i + 1 < dst_size; ++i)
    {
        char c = src[i];
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        dst[i] = c;
    }
    dst[i] = '\0';

    while (i > 0 && dst[i - 1] == ' ')
        dst[--i] = '\0';
}

static void append_history_line_locked(const char *text)
{
    if (!text)
        return;

    size_t slot = (g_logHistoryHead + g_logHistoryCount) % LOG_HISTORY_CAPACITY;
    if (g_logHistoryCount >= LOG_HISTORY_CAPACITY)
    {
        slot = g_logHistoryHead;
        g_logHistoryHead = (g_logHistoryHead + 1) % LOG_HISTORY_CAPACITY;
    }
    else
    {
        g_logHistoryCount++;
    }

    normalize_log_text(text, g_logHistory[slot].text, sizeof(g_logHistory[slot].text));
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
            char line[LOG_HISTORY_MESSAGE_MAX];
            line[0] = '\0';
            snprintf(line, sizeof(line), "[%s] ", level_label(entry.level));
            size_t prefix_len = strlen(line);
            normalize_log_text(entry.text, line + prefix_len, sizeof(line) - prefix_len);

            mutexLock(&g_logMutex);
            append_history_line_locked(line);
            bool mirror_to_stdio = g_logMirrorToStdio;
            mutexUnlock(&g_logMutex);

            if (mirror_to_stdio)
            {
                fprintf(stderr, "%s\n", line);
                fflush(stderr);
            }
            continue;
        }

        if (dropped > 0)
        {
            char line[LOG_HISTORY_MESSAGE_MAX];
            snprintf(line, sizeof(line), "[WARN] log queue full, dropped %u messages", dropped);

            mutexLock(&g_logMutex);
            append_history_line_locked(line);
            bool mirror_to_stdio = g_logMirrorToStdio;
            mutexUnlock(&g_logMutex);

            if (mirror_to_stdio)
            {
                fprintf(stderr, "%s\n", line);
                fflush(stderr);
            }
            continue;
        }

        break;
    }
}

size_t log_history_count(void)
{
    mutexLock(&g_logMutex);
    size_t count = g_logHistoryCount;
    mutexUnlock(&g_logMutex);
    return count;
}

bool log_history_get_line(size_t index, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;

    out[0] = '\0';

    mutexLock(&g_logMutex);
    if (index >= g_logHistoryCount)
    {
        mutexUnlock(&g_logMutex);
        return false;
    }

    size_t slot = (g_logHistoryHead + index) % LOG_HISTORY_CAPACITY;
    snprintf(out, out_size, "%s", g_logHistory[slot].text);
    mutexUnlock(&g_logMutex);
    return true;
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

void log_set_stdio_mirror(bool enabled)
{
    mutexLock(&g_logMutex);
    g_logMirrorToStdio = enabled;
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
