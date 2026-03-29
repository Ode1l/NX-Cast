#include "log.h"

#include <switch.h>

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_QUEUE_CAPACITY 2048
#define LOG_MESSAGE_MAX 1024
#define LOG_HISTORY_CAPACITY 4096
#define LOG_HISTORY_MESSAGE_MAX 512
#define LOG_WORKER_STACK_SIZE (32 * 1024)
#define LOG_REMOTE_CONNECT_RETRY_NS (1000ULL * 1000ULL * 1000ULL)
#define LOG_REMOTE_IO_TIMEOUT_MS 200

typedef struct
{
    LogLevel level;
    char text[LOG_MESSAGE_MAX];
} LogEntry;

typedef struct
{
    char text[LOG_HISTORY_MESSAGE_MAX];
} LogHistoryEntry;

static Mutex g_logMutex;
static CondVar g_logCondVar;
static Thread g_logThread;
static bool g_logThreadStarted = false;
static bool g_logWorkerRunning = false;
static bool g_logStopRequested = false;
static bool g_logEnabled = true;
static LogLevel g_logMinLevel = LOG_LEVEL_INFO;
static bool g_logMirrorToStdio = false;

static LogEntry g_logQueue[LOG_QUEUE_CAPACITY];
static size_t g_logHead = 0;
static size_t g_logTail = 0;
static size_t g_logCount = 0;
static unsigned int g_logDroppedCount = 0;

static LogHistoryEntry g_logHistory[LOG_HISTORY_CAPACITY];
static size_t g_logHistoryHead = 0;
static size_t g_logHistoryCount = 0;

static bool g_remoteConfigured = false;
static uint32_t g_remoteHostAddrBe = 0;
static uint16_t g_remotePort = 0;
static int g_remoteSock = -1;
static uint64_t g_remoteNextRetryTick = 0;

static void close_remote_socket_locked(void)
{
    if (g_remoteSock >= 0)
    {
        close(g_remoteSock);
        g_remoteSock = -1;
    }
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

static void format_log_line(LogLevel level, const char *text, char *line, size_t line_size)
{
    if (!line || line_size == 0)
        return;

    line[0] = '\0';
    snprintf(line, line_size, "[%s] ", level_label(level));
    size_t prefix_len = strlen(line);
    normalize_log_text(text, line + prefix_len, line_size - prefix_len);
}

static bool remote_connect_if_needed(void)
{
    uint32_t host_addr_be = 0;
    uint16_t port = 0;

    mutexLock(&g_logMutex);
    if (!g_remoteConfigured)
    {
        mutexUnlock(&g_logMutex);
        return false;
    }

    if (g_remoteSock >= 0)
    {
        mutexUnlock(&g_logMutex);
        return true;
    }

    uint64_t now = armGetSystemTick();
    if (g_remoteNextRetryTick != 0 && now < g_remoteNextRetryTick)
    {
        mutexUnlock(&g_logMutex);
        return false;
    }

    host_addr_be = g_remoteHostAddrBe;
    port = g_remotePort;
    mutexUnlock(&g_logMutex);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        mutexLock(&g_logMutex);
        g_remoteNextRetryTick = armGetSystemTick() + armNsToTicks(LOG_REMOTE_CONNECT_RETRY_NS);
        mutexUnlock(&g_logMutex);
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = LOG_REMOTE_IO_TIMEOUT_MS * 1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = host_addr_be;

    if (connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        mutexLock(&g_logMutex);
        g_remoteNextRetryTick = armGetSystemTick() + armNsToTicks(LOG_REMOTE_CONNECT_RETRY_NS);
        mutexUnlock(&g_logMutex);
        return false;
    }

    mutexLock(&g_logMutex);
    if (!g_remoteConfigured || g_remoteHostAddrBe != host_addr_be || g_remotePort != port)
    {
        mutexUnlock(&g_logMutex);
        close(sock);
        return false;
    }
    g_remoteSock = sock;
    g_remoteNextRetryTick = 0;
    mutexUnlock(&g_logMutex);
    return true;
}

static void remote_send_line(const char *line)
{
    if (!line)
        return;

    if (!remote_connect_if_needed())
        return;

    mutexLock(&g_logMutex);
    int sock = g_remoteSock;
    mutexUnlock(&g_logMutex);
    if (sock < 0)
        return;

    size_t len = strlen(line);
    if (send(sock, line, len, 0) < 0 || send(sock, "\n", 1, 0) < 0)
    {
        mutexLock(&g_logMutex);
        close_remote_socket_locked();
        g_remoteNextRetryTick = armGetSystemTick() + armNsToTicks(LOG_REMOTE_CONNECT_RETRY_NS);
        mutexUnlock(&g_logMutex);
    }
}

static void log_worker_thread(void *arg)
{
    (void)arg;

    while (true)
    {
        LogEntry entry;
        bool has_entry = false;
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

        mirror_to_stdio = g_logMirrorToStdio;
        mutexUnlock(&g_logMutex);

        if (has_entry)
        {
            char line[LOG_HISTORY_MESSAGE_MAX];
            format_log_line(entry.level, entry.text, line, sizeof(line));

            mutexLock(&g_logMutex);
            append_history_line_locked(line);
            mutexUnlock(&g_logMutex);

            if (mirror_to_stdio)
            {
                fprintf(stderr, "%s\n", line);
                fflush(stderr);
            }
            remote_send_line(line);
            continue;
        }

        if (dropped > 0)
        {
            char line[LOG_HISTORY_MESSAGE_MAX];
            snprintf(line, sizeof(line), "[WARN] log queue full, dropped %u messages", dropped);

            mutexLock(&g_logMutex);
            append_history_line_locked(line);
            mutexUnlock(&g_logMutex);

            if (mirror_to_stdio)
            {
                fprintf(stderr, "%s\n", line);
                fflush(stderr);
            }
            remote_send_line(line);
        }
    }

    mutexLock(&g_logMutex);
    close_remote_socket_locked();
    g_logWorkerRunning = false;
    mutexUnlock(&g_logMutex);
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
    g_logWorkerRunning = true;
    mutexUnlock(&g_logMutex);

    Result rc = threadCreate(&g_logThread,
                             log_worker_thread,
                             NULL,
                             NULL,
                             LOG_WORKER_STACK_SIZE,
                             0x2D,
                             -2);
    if (R_FAILED(rc))
    {
        mutexLock(&g_logMutex);
        g_logWorkerRunning = false;
        mutexUnlock(&g_logMutex);
        return false;
    }

    rc = threadStart(&g_logThread);
    if (R_FAILED(rc))
    {
        threadClose(&g_logThread);
        mutexLock(&g_logMutex);
        g_logWorkerRunning = false;
        mutexUnlock(&g_logMutex);
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
        close_remote_socket_locked();
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
    g_logWorkerRunning = false;
    close_remote_socket_locked();
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

    char formatted[LOG_MESSAGE_MAX];
    int written = vsnprintf(formatted, sizeof(formatted), fmt, args);
    if (written < 0)
        return;

    mutexLock(&g_logMutex);
    if (g_logCount >= LOG_QUEUE_CAPACITY)
    {
        g_logHead = (g_logHead + 1) % LOG_QUEUE_CAPACITY;
        g_logCount--;
        g_logDroppedCount++;
    }

    LogEntry *entry = &g_logQueue[g_logTail];
    entry->level = level;
    snprintf(entry->text, sizeof(entry->text), "%s", formatted);
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

bool log_set_remote_host(uint32_t host_addr_be, uint16_t port)
{
    mutexLock(&g_logMutex);
    g_remoteConfigured = true;
    g_remoteHostAddrBe = host_addr_be;
    g_remotePort = port;
    close_remote_socket_locked();
    g_remoteNextRetryTick = 0;
    mutexUnlock(&g_logMutex);
    condvarWakeOne(&g_logCondVar);
    return true;
}

void log_clear_remote_host(void)
{
    mutexLock(&g_logMutex);
    g_remoteConfigured = false;
    g_remoteHostAddrBe = 0;
    g_remotePort = 0;
    g_remoteNextRetryTick = 0;
    close_remote_socket_locked();
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
