#include "log.h"

#include <switch.h>

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_QUEUE_CAPACITY 2048
#define LOG_HISTORY_CAPACITY 4096
#define LOG_WORKER_STACK_SIZE (32 * 1024)

typedef struct
{
    char *line;
} LogEntry;

static Mutex g_logMutex;
static CondVar g_logCondVar;
static Thread g_logThread;
static bool g_logThreadStarted = false;
static bool g_logStopRequested = false;
static bool g_logEnabled = true;
static LogLevel g_logMinLevel = LOG_LEVEL_INFO;
static bool g_logMirrorToStdio = false;

static LogEntry g_logQueue[LOG_QUEUE_CAPACITY];
static size_t g_logHead = 0;
static size_t g_logTail = 0;
static size_t g_logCount = 0;
static unsigned int g_logDroppedCount = 0;

static LogEntry g_logHistory[LOG_HISTORY_CAPACITY];
static size_t g_logHistoryHead = 0;
static size_t g_logHistoryCount = 0;

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

static char *strdup_local(const char *src)
{
    if (!src)
        return NULL;

    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;

    memcpy(copy, src, len + 1);
    return copy;
}

static void normalize_log_text_inplace(char *text)
{
    if (!text)
        return;

    size_t write = 0;
    for (size_t read = 0; text[read] != '\0'; ++read)
    {
        char c = text[read];
        if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
        text[write++] = c;
    }

    while (write > 0 && text[write - 1] == ' ')
        --write;

    text[write] = '\0';
}

static char *alloc_vformatted(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0)
        return NULL;

    char *text = (char *)malloc((size_t)needed + 1);
    if (!text)
        return NULL;

    va_copy(copy, args);
    vsnprintf(text, (size_t)needed + 1, fmt, copy);
    va_end(copy);
    return text;
}

static char *build_log_line(LogLevel level, const char *fmt, va_list args)
{
    char *body = alloc_vformatted(fmt, args);
    if (!body)
        return NULL;

    normalize_log_text_inplace(body);

    const char *label = level_label(level);
    int needed = snprintf(NULL, 0, "[%s] %s", label, body);
    if (needed < 0)
    {
        free(body);
        return NULL;
    }

    char *line = (char *)malloc((size_t)needed + 1);
    if (!line)
    {
        free(body);
        return NULL;
    }

    snprintf(line, (size_t)needed + 1, "[%s] %s", label, body);
    free(body);
    return line;
}

static void clear_entry(LogEntry *entry)
{
    if (!entry || !entry->line)
        return;

    free(entry->line);
    entry->line = NULL;
}

static void append_history_locked(char *line)
{
    if (!line)
        return;

    size_t slot = (g_logHistoryHead + g_logHistoryCount) % LOG_HISTORY_CAPACITY;
    if (g_logHistoryCount >= LOG_HISTORY_CAPACITY)
    {
        slot = g_logHistoryHead;
        clear_entry(&g_logHistory[slot]);
        g_logHistoryHead = (g_logHistoryHead + 1) % LOG_HISTORY_CAPACITY;
    }
    else
    {
        g_logHistoryCount++;
    }

    g_logHistory[slot].line = line;
}

static void clear_queue_locked(void)
{
    for (size_t i = 0; i < g_logCount; ++i)
    {
        size_t slot = (g_logHead + i) % LOG_QUEUE_CAPACITY;
        clear_entry(&g_logQueue[slot]);
    }

    g_logHead = 0;
    g_logTail = 0;
    g_logCount = 0;
    g_logDroppedCount = 0;
}

static void log_worker_thread(void *arg)
{
    (void)arg;

    while (true)
    {
        char *line = NULL;
        unsigned int dropped = 0;
        bool mirror_to_stdio = false;

        mutexLock(&g_logMutex);
        while (!g_logStopRequested && g_logCount == 0 && g_logDroppedCount == 0)
            condvarWait(&g_logCondVar, &g_logMutex);

        if (g_logStopRequested && g_logCount == 0 && g_logDroppedCount == 0)
        {
            mutexUnlock(&g_logMutex);
            break;
        }

        if (g_logCount > 0)
        {
            line = g_logQueue[g_logHead].line;
            g_logQueue[g_logHead].line = NULL;
            g_logHead = (g_logHead + 1) % LOG_QUEUE_CAPACITY;
            g_logCount--;
        }
        else if (g_logDroppedCount > 0)
        {
            dropped = g_logDroppedCount;
            g_logDroppedCount = 0;
        }

        mirror_to_stdio = g_logMirrorToStdio;
        mutexUnlock(&g_logMutex);

        if (line)
        {
            mutexLock(&g_logMutex);
            append_history_locked(line);
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
            char fallback[96];
            snprintf(fallback, sizeof(fallback), "[WARN] log queue full, dropped %u messages", dropped);
            char *dropped_line = strdup_local(fallback);
            if (!dropped_line)
                continue;

            mutexLock(&g_logMutex);
            append_history_locked(dropped_line);
            mutexUnlock(&g_logMutex);

            if (mirror_to_stdio)
            {
                fprintf(stderr, "%s\n", dropped_line);
                fflush(stderr);
            }
        }
    }

    threadExit();
}

bool log_runtime_init(void)
{
    mutexLock(&g_logMutex);
    if (g_logThreadStarted)
    {
        mutexUnlock(&g_logMutex);
        return true;
    }

    g_logStopRequested = false;
    mutexUnlock(&g_logMutex);

    Result rc = threadCreate(&g_logThread,
                             log_worker_thread,
                             NULL,
                             NULL,
                             LOG_WORKER_STACK_SIZE,
                             0x2D,
                             -2);
    if (R_FAILED(rc))
        return false;

    rc = threadStart(&g_logThread);
    if (R_FAILED(rc))
    {
        threadClose(&g_logThread);
        return false;
    }

    mutexLock(&g_logMutex);
    g_logThreadStarted = true;
    mutexUnlock(&g_logMutex);
    return true;
}

void log_runtime_shutdown(void)
{
    mutexLock(&g_logMutex);
    if (!g_logThreadStarted)
    {
        mutexUnlock(&g_logMutex);
        return;
    }

    g_logStopRequested = true;
    condvarWakeAll(&g_logCondVar);
    mutexUnlock(&g_logMutex);

    threadWaitForExit(&g_logThread);
    threadClose(&g_logThread);

    mutexLock(&g_logMutex);
    g_logThreadStarted = false;
    clear_queue_locked();
    mutexUnlock(&g_logMutex);
}

__attribute__((constructor)) static void log_runtime_globals_init(void)
{
    mutexInit(&g_logMutex);
    condvarInit(&g_logCondVar);
}

void vlog_write(LogLevel level, const char *fmt, va_list args)
{
    if (!fmt)
        return;

    mutexLock(&g_logMutex);
    bool enabled = g_logEnabled;
    LogLevel min_level = g_logMinLevel;
    mutexUnlock(&g_logMutex);

    if (!enabled || level < min_level)
        return;

    char *line = build_log_line(level, fmt, args);
    if (!line)
        return;

    mutexLock(&g_logMutex);
    if (g_logCount >= LOG_QUEUE_CAPACITY)
    {
        clear_entry(&g_logQueue[g_logHead]);
        g_logHead = (g_logHead + 1) % LOG_QUEUE_CAPACITY;
        g_logCount--;
        g_logDroppedCount++;
    }

    g_logQueue[g_logTail].line = line;
    g_logTail = (g_logTail + 1) % LOG_QUEUE_CAPACITY;
    g_logCount++;
    condvarWakeOne(&g_logCondVar);
    mutexUnlock(&g_logMutex);
}

void log_flush(void)
{
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
    snprintf(out, out_size, "%s", g_logHistory[slot].line ? g_logHistory[slot].line : "");
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
