#include "log.h"
#include "mirror.h"

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
static LogLevel g_logMinLevel = NXCAST_LOG_LEVEL_DEFAULT;
static bool g_logMirrorToStdio = false;
static int g_logMirrorSocket = -1;
static uint64_t g_logEnqueued = 0;
static uint64_t g_logProcessed = 0;
static uint64_t g_logQueueDroppedTotal = 0;
static uint64_t g_logMirrorDropped = 0;
static uint64_t g_logMirrorFailures = 0;
static uint64_t g_logWorkerHeartbeatMs = 0;
static size_t g_logQueueHighWatermark = 0;
static bool g_logWorkerWaiting = false;

static LogEntry g_logQueue[LOG_QUEUE_CAPACITY];
static size_t g_logHead = 0;
static size_t g_logTail = 0;
static size_t g_logCount = 0;
static unsigned int g_logDroppedCount = 0;

static LogEntry g_logHistory[LOG_HISTORY_CAPACITY];
static size_t g_logHistoryHead = 0;
static size_t g_logHistoryCount = 0;

static uint64_t log_monotonic_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
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

static void write_line_to_mirror(const char *line, bool mirror_to_socket,
                                 int mirror_socket)
{
    bool mirror_dropped = false;
    bool mirror_failed = false;
    LogMirrorWriteResult mirror_result = LOG_MIRROR_WRITE_OK;

    if (mirror_to_socket && mirror_socket >= 0)
    {
        mirror_result = log_mirror_write_nonblocking(mirror_socket, line);
        mirror_dropped = mirror_result != LOG_MIRROR_WRITE_OK;
        mirror_failed = mirror_result == LOG_MIRROR_WRITE_FAILED;
    }

    mutexLock(&g_logMutex);
    g_logProcessed++;
    if (mirror_dropped)
        g_logMirrorDropped++;
    if (mirror_failed)
        g_logMirrorFailures++;
    mutexUnlock(&g_logMutex);
}

static void log_worker_thread(void *arg)
{
    (void)arg;

    while (true)
    {
        char *line = NULL;
        unsigned int dropped = 0;
        bool mirror_to_stdio = false;
        int mirror_socket = -1;

        mutexLock(&g_logMutex);
        g_logWorkerHeartbeatMs = log_monotonic_ms();
        g_logWorkerWaiting = true;
        while (!g_logStopRequested && g_logCount == 0 && g_logDroppedCount == 0)
            condvarWait(&g_logCondVar, &g_logMutex);
        g_logWorkerWaiting = false;
        g_logWorkerHeartbeatMs = log_monotonic_ms();

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
        mirror_socket = g_logMirrorSocket;
        mutexUnlock(&g_logMutex);

        if (line)
        {
            mutexLock(&g_logMutex);
            append_history_locked(line);
            mutexUnlock(&g_logMutex);

            write_line_to_mirror(line, mirror_to_stdio, mirror_socket);
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

            write_line_to_mirror(dropped_line, mirror_to_stdio, mirror_socket);
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
    g_logWorkerHeartbeatMs = log_monotonic_ms();
    g_logWorkerWaiting = false;
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
        g_logMirrorToStdio = false;
        g_logMirrorSocket = -1;
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
    g_logWorkerWaiting = false;
    clear_queue_locked();
    g_logMirrorToStdio = false;
    g_logMirrorSocket = -1;
    mutexUnlock(&g_logMutex);
}

__attribute__((constructor)) static void log_runtime_globals_init(void)
{
    mutexInit(&g_logMutex);
    condvarInit(&g_logCondVar);
}

void log_set_socket_mirror(int socket_fd)
{
    if (socket_fd >= 0 && !log_mirror_configure_nonblocking(socket_fd))
        socket_fd = -1;

    mutexLock(&g_logMutex);
    g_logMirrorSocket = socket_fd;
    if (socket_fd < 0)
        g_logMirrorToStdio = false;
    mutexUnlock(&g_logMutex);
}

bool log_get_runtime_stats(LogRuntimeStats *stats_out)
{
    uint64_t now_ms;

    if (!stats_out)
        return false;

    mutexLock(&g_logMutex);
    stats_out->enqueued = g_logEnqueued;
    stats_out->processed = g_logProcessed;
    stats_out->queue_dropped = g_logQueueDroppedTotal;
    stats_out->mirror_dropped = g_logMirrorDropped;
    stats_out->mirror_failures = g_logMirrorFailures;
    stats_out->queue_depth = g_logCount;
    stats_out->queue_high_watermark = g_logQueueHighWatermark;
    now_ms = log_monotonic_ms();
    stats_out->worker_heartbeat_age_ms =
        g_logWorkerHeartbeatMs > 0 && now_ms >= g_logWorkerHeartbeatMs
            ? now_ms - g_logWorkerHeartbeatMs
            : 0;
    stats_out->worker_running = g_logThreadStarted;
    stats_out->worker_waiting = g_logWorkerWaiting;
    stats_out->socket_mirror_enabled = g_logMirrorToStdio &&
                                       g_logMirrorSocket >= 0;
    mutexUnlock(&g_logMutex);
    return true;
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
        g_logQueueDroppedTotal++;
    }

    g_logQueue[g_logTail].line = line;
    g_logTail = (g_logTail + 1) % LOG_QUEUE_CAPACITY;
    g_logCount++;
    if (g_logCount > g_logQueueHighWatermark)
        g_logQueueHighWatermark = g_logCount;
    g_logEnqueued++;
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

const char *log_get_mpv_level(void)
{
    switch (log_get_level())
    {
    case LOG_LEVEL_DEBUG:
        return "debug";
    case LOG_LEVEL_INFO:
        return "info";
    case LOG_LEVEL_WARN:
        return "warn";
    case LOG_LEVEL_ERROR:
    default:
        return "error";
    }
}

void log_set_stdio_mirror(bool enabled)
{
    mutexLock(&g_logMutex);
    g_logMirrorToStdio = enabled && g_logMirrorSocket >= 0;
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
