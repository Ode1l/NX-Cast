#include "event_server.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "handler_internal.h"
#include "log/log.h"
#include "player/renderer.h"
#include "soap_writer.h"

#define EVENT_MAX_SUBSCRIPTIONS 8
// Eventing builds XML and outgoing HTTP requests; use a safer stack floor than
// the historical 0x8000.
#define EVENT_THREAD_STACK_SIZE 0x10000
#define EVENT_DEFAULT_TIMEOUT_SECONDS 1800
#define EVENT_MIN_TIMEOUT_SECONDS 60
#define EVENT_MAX_TIMEOUT_SECONDS 3600
#define EVENT_NOTIFY_RETRY_SLEEP_MS 100
#define EVENT_NOTIFY_IO_TIMEOUT_SEC 1
#define DLNA_SERVER_INFO "NintendoSwitch/1.0 UPnP/1.0 NX-Cast/0.1"

typedef enum
{
    EVENT_SERVICE_INVALID = -1,
    EVENT_SERVICE_AVTRANSPORT = 0,
    EVENT_SERVICE_RENDERINGCONTROL,
    EVENT_SERVICE_CONNECTIONMANAGER,
    EVENT_SERVICE_COUNT
} EventService;

typedef struct
{
    bool active;
    EventService service;
    char *sid;
    char *callback_url;
    char *callback_host;
    uint16_t callback_port;
    char *callback_path;
    uint32_t seq;
    uint64_t expiry_ms;
} EventSubscription;

typedef struct
{
    bool in_use;
    EventService service;
    char *sid;
    char *callback_host;
    uint16_t callback_port;
    char *callback_path;
    uint32_t seq;
} EventNotifyTarget;

static Mutex g_event_mutex;
static CondVar g_event_cond;
static bool g_event_sync_ready = false;
static Thread g_event_thread;
static bool g_event_thread_started = false;
static bool g_event_stop_requested = false;
static EventSubscription g_event_subscriptions[EVENT_MAX_SUBSCRIPTIONS];
static bool g_avtransport_state_dirty = false;
static bool g_renderingcontrol_dirty = false;
static bool g_connectionmanager_dirty = false;
static uint32_t g_next_sid = 1;

static void event_thread_main(void *arg);

static void event_subscription_clear(EventSubscription *subscription)
{
    if (!subscription)
        return;

    free(subscription->sid);
    free(subscription->callback_url);
    free(subscription->callback_host);
    free(subscription->callback_path);
    memset(subscription, 0, sizeof(*subscription));
}

static void event_notify_target_clear(EventNotifyTarget *target)
{
    if (!target)
        return;

    free(target->sid);
    free(target->callback_host);
    free(target->callback_path);
    memset(target, 0, sizeof(*target));
}

static void event_clear_targets(EventNotifyTarget *targets, size_t count)
{
    if (!targets)
        return;

    for (size_t i = 0; i < count; ++i)
        event_notify_target_clear(&targets[i]);
}

static uint64_t event_now_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static void event_ensure_sync(void)
{
    if (g_event_sync_ready)
        return;

    mutexInit(&g_event_mutex);
    condvarInit(&g_event_cond);
    g_event_sync_ready = true;
}

static const char *event_service_name(EventService service)
{
    switch (service)
    {
    case EVENT_SERVICE_AVTRANSPORT:
        return "AVTransport";
    case EVENT_SERVICE_RENDERINGCONTROL:
        return "RenderingControl";
    case EVENT_SERVICE_CONNECTIONMANAGER:
        return "ConnectionManager";
    default:
        return "Unknown";
    }
}

static EventService event_service_from_path(const char *path)
{
    const char *name = NULL;
    size_t len = 0;

    if (!path)
        return EVENT_SERVICE_INVALID;

    if (strncmp(path, "/upnp/event/", strlen("/upnp/event/")) == 0)
    {
        name = path + strlen("/upnp/event/");
        len = strcspn(name, " ?\r\n");
    }
    else if (path[0] == '/')
    {
        const char *service = path + 1;
        const char *suffix = strchr(service, '/');
        if (suffix && strcmp(suffix, "/event") == 0)
        {
            name = service;
            len = (size_t)(suffix - service);
        }
    }

    if (!name || len == 0)
        return EVENT_SERVICE_INVALID;

    if (len == strlen("AVTransport") && strncasecmp(name, "AVTransport", len) == 0)
        return EVENT_SERVICE_AVTRANSPORT;
    if (len == strlen("RenderingControl") && strncasecmp(name, "RenderingControl", len) == 0)
        return EVENT_SERVICE_RENDERINGCONTROL;
    if (len == strlen("ConnectionManager") && strncasecmp(name, "ConnectionManager", len) == 0)
        return EVENT_SERVICE_CONNECTIONMANAGER;
    return EVENT_SERVICE_INVALID;
}

static void event_trim_in_place(char *value)
{
    size_t len;
    size_t start = 0;

    if (!value)
        return;

    len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r' || value[len - 1] == '\n'))
        value[--len] = '\0';

    while (value[start] == ' ' || value[start] == '\t')
        ++start;

    if (start > 0)
        memmove(value, value + start, strlen(value + start) + 1);
}

static bool event_get_header_value_alloc(const char *request, const char *header, char **out)
{
    size_t header_len;
    const char *cursor;

    if (!request || !header || !out)
        return false;

    *out = NULL;
    header_len = strlen(header);
    cursor = request;

    while (*cursor)
    {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (line_len == 0)
            break;

        if (line_len > header_len + 1 &&
            strncasecmp(cursor, header, header_len) == 0 &&
            cursor[header_len] == ':')
        {
            const char *value_start = cursor + header_len + 1;
            size_t copy_len;
            char *value;

            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;

            copy_len = line_end ? (size_t)(line_end - value_start) : strlen(value_start);
            value = malloc(copy_len + 1);
            if (!value)
                return false;

            memcpy(value, value_start, copy_len);
            value[copy_len] = '\0';
            event_trim_in_place(value);
            if (value[0] == '\0')
            {
                free(value);
                return false;
            }

            *out = value;
            return true;
        }

        if (!line_end)
            break;
        cursor = line_end + 2;
    }

    return false;
}

static bool event_parse_timeout_seconds(const char *request, int *out_seconds)
{
    char *timeout = NULL;
    long seconds;
    char *value = NULL;
    char *end_ptr = NULL;

    if (!out_seconds)
        return false;

    *out_seconds = EVENT_DEFAULT_TIMEOUT_SECONDS;
    if (!request || !event_get_header_value_alloc(request, "TIMEOUT", &timeout))
        return true;

    if (strcasecmp(timeout, "infinite") == 0)
    {
        free(timeout);
        *out_seconds = EVENT_MAX_TIMEOUT_SECONDS;
        return true;
    }

    if (strncasecmp(timeout, "Second-", 7) == 0)
        value = timeout + 7;
    else
        value = timeout;

    seconds = strtol(value, &end_ptr, 10);
    if (!end_ptr || *end_ptr != '\0' || seconds <= 0)
    {
        free(timeout);
        return false;
    }

    if (seconds < EVENT_MIN_TIMEOUT_SECONDS)
        seconds = EVENT_MIN_TIMEOUT_SECONDS;
    if (seconds > EVENT_MAX_TIMEOUT_SECONDS)
        seconds = EVENT_MAX_TIMEOUT_SECONDS;
    *out_seconds = (int)seconds;
    free(timeout);
    return true;
}

static bool event_parse_callback_url(const char *header_value,
                                     char **normalized,
                                     char **host,
                                     uint16_t *port,
                                     char **path)
{
    char *value = NULL;
    const char *url;
    const char *host_start;
    const char *path_start;
    const char *port_sep;
    size_t host_len;
    if (!header_value || !normalized || !host || !port || !path)
        return false;

    *normalized = NULL;
    *host = NULL;
    *path = NULL;
    value = strdup(header_value);
    if (!value)
        return false;
    event_trim_in_place(value);

    if (value[0] == '<')
    {
        size_t len = strlen(value);
        if (len < 3 || value[len - 1] != '>')
            return false;
        memmove(value, value + 1, len - 2);
        value[len - 2] = '\0';
    }

    if (strncasecmp(value, "http://", 7) != 0)
    {
        free(value);
        return false;
    }

    url = value + 7;
    host_start = url;
    path_start = strchr(host_start, '/');
    if (!path_start)
        path_start = host_start + strlen(host_start);

    port_sep = memchr(host_start, ':', (size_t)(path_start - host_start));
    if (port_sep)
        host_len = (size_t)(port_sep - host_start);
    else
        host_len = (size_t)(path_start - host_start);
    if (host_len == 0)
    {
        free(value);
        return false;
    }

    *host = strndup(host_start, host_len);
    if (!*host)
    {
        free(value);
        return false;
    }

    if (port_sep)
    {
        long parsed_port;
        char *end_ptr = NULL;
        parsed_port = strtol(port_sep + 1, &end_ptr, 10);
        if (!end_ptr || end_ptr != path_start || parsed_port <= 0 || parsed_port > 65535)
        {
            free(value);
            free(*host);
            *host = NULL;
            return false;
        }
        *port = (uint16_t)parsed_port;
    }
    else
    {
        *port = 80;
    }

    if (*path_start == '\0')
        *path = strdup("/");
    else
        *path = strdup(path_start);
    if (!*path)
    {
        free(value);
        free(*host);
        *host = NULL;
        return false;
    }

    *normalized = soap_handler_strdup_printf("http://%s:%u%s", *host, (unsigned)*port, *path);
    if (!*normalized)
    {
        free(value);
        free(*host);
        free(*path);
        *host = NULL;
        *path = NULL;
        return false;
    }

    free(value);
    return true;
}

static bool event_build_response(int status,
                                 const char *status_text,
                                 const char *extra_headers,
                                 const char *body,
                                 char *response,
                                 size_t response_size,
                                 size_t *response_len)
{
    const char *payload = body ? body : "";
    const char *headers = extra_headers ? extra_headers : "";
    size_t body_len;
    int written;

    if (!status_text || !response || response_size == 0 || !response_len)
        return false;

    body_len = strlen(payload);
    written = snprintf(response, response_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: text/xml; charset=\"utf-8\"\r\n"
                       "Content-Length: %zu\r\n"
                       "Server: %s\r\n"
                       "%s"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       status,
                       status_text,
                       body_len,
                       DLNA_SERVER_INFO,
                       headers,
                       payload);
    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    return true;
}

static void event_prune_expired_locked(uint64_t now_ms)
{
    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS; ++i)
    {
        if (!g_event_subscriptions[i].active)
            continue;
        if (g_event_subscriptions[i].expiry_ms <= now_ms)
        {
            log_info("[event] expired service=%s sid=%s callback=%s\n",
                     event_service_name(g_event_subscriptions[i].service),
                     g_event_subscriptions[i].sid,
                     g_event_subscriptions[i].callback_url);
            event_subscription_clear(&g_event_subscriptions[i]);
        }
    }
}

static ssize_t event_find_subscription_by_sid_locked(const char *sid)
{
    if (!sid || sid[0] == '\0')
        return -1;

    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS; ++i)
    {
        if (!g_event_subscriptions[i].active)
            continue;
        if (strcasecmp(g_event_subscriptions[i].sid, sid) == 0)
            return (ssize_t)i;
    }
    return -1;
}

static ssize_t event_find_free_subscription_locked(void)
{
    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS; ++i)
    {
        if (!g_event_subscriptions[i].active)
            return (ssize_t)i;
    }
    return -1;
}

static void event_mark_dirty_locked(EventService service)
{
    switch (service)
    {
    case EVENT_SERVICE_AVTRANSPORT:
        g_avtransport_state_dirty = true;
        break;
    case EVENT_SERVICE_RENDERINGCONTROL:
        g_renderingcontrol_dirty = true;
        break;
    case EVENT_SERVICE_CONNECTIONMANAGER:
        g_connectionmanager_dirty = true;
        break;
    default:
        break;
    }
}

static bool event_append_val_element(SoapActionOutput *out, const char *tag, const char *value)
{
    return soap_writer_appendf(out, "<%s val=\"", tag) &&
           soap_writer_append_escaped(out, value ? value : "") &&
           soap_writer_append_raw(out, "\"/>");
}

static bool event_build_avtransport_last_change(SoapActionOutput *out)
{
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!out)
        return false;

    return soap_writer_append_raw(out, "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/AVT/\"><InstanceID val=\"0\">") &&
           event_append_val_element(out, "TransportState", state->transport_state) &&
           event_append_val_element(out, "TransportStatus", state->transport_status) &&
           soap_writer_appendf(out, "<CurrentTrack val=\"%d\"/>", state->current_track) &&
           soap_writer_appendf(out, "<NumberOfTracks val=\"%d\"/>", state->number_of_tracks) &&
           event_append_val_element(out, "CurrentMediaDuration", state->current_media_duration) &&
           event_append_val_element(out, "CurrentTrackDuration", state->current_track_duration) &&
           soap_writer_append_raw(out, "</InstanceID></Event>");
}

static bool event_build_renderingcontrol_last_change(SoapActionOutput *out)
{
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!out)
        return false;

    return soap_writer_append_raw(out, "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/RCS/\"><InstanceID val=\"0\">") &&
           soap_writer_appendf(out, "<Volume channel=\"Master\" val=\"%d\"/>", state->volume) &&
           soap_writer_appendf(out, "<Mute channel=\"Master\" val=\"%d\"/>", state->mute ? 1 : 0) &&
           soap_writer_append_raw(out, "</InstanceID></Event>");
}

static bool event_build_propertyset(EventService service, SoapActionOutput *out)
{
    SoapActionOutput last_change = {0};
    bool ok = false;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!out)
        return false;

    soap_writer_clear(out);
    if (!soap_writer_append_raw(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                     "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">"))
        return false;

    switch (service)
    {
    case EVENT_SERVICE_AVTRANSPORT:
        ok = event_build_avtransport_last_change(&last_change) &&
             soap_writer_append_raw(out, "<e:property><LastChange>") &&
             soap_writer_append_escaped(out, last_change.output_xml) &&
             soap_writer_append_raw(out, "</LastChange></e:property>");
        break;
    case EVENT_SERVICE_RENDERINGCONTROL:
        ok = event_build_renderingcontrol_last_change(&last_change) &&
             soap_writer_append_raw(out, "<e:property><LastChange>") &&
             soap_writer_append_escaped(out, last_change.output_xml) &&
             soap_writer_append_raw(out, "</LastChange></e:property>");
        break;
    case EVENT_SERVICE_CONNECTIONMANAGER:
        ok = soap_writer_append_raw(out, "<e:property><CurrentConnectionIDs>") &&
             soap_writer_append_escaped(out, state->current_connection_ids) &&
             soap_writer_append_raw(out, "</CurrentConnectionIDs></e:property>");
        break;
    default:
        ok = false;
        break;
    }

    soap_writer_dispose(&last_change);
    if (!ok)
    {
        soap_writer_clear(out);
        return false;
    }

    return soap_writer_append_raw(out, "</e:propertyset>");
}

static bool event_send_all(int sock, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t chunk = send(sock, buf + sent, len - sent, 0);
        if (chunk <= 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        sent += (size_t)chunk;
    }

    return true;
}

static bool event_send_notify(const EventNotifyTarget *target, EventService service)
{
    SoapActionOutput body = {0};
    bool success = false;
    int sock = -1;
    struct sockaddr_in addr;
    struct timeval timeout;
    char *request = NULL;

    if (!target || !target->in_use)
        return false;

    if (!event_build_propertyset(service, &body))
    {
        log_warn("[event] build propertyset failed service=%s sid=%s\n",
                 event_service_name(service), target->sid);
        return false;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        goto cleanup;

    timeout.tv_sec = EVENT_NOTIFY_IO_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target->callback_port);
    if (inet_pton(AF_INET, target->callback_host, &addr.sin_addr) != 1)
    {
        log_warn("[event] callback host is not IPv4 literal service=%s sid=%s host=%s\n",
                 event_service_name(service), target->sid, target->callback_host);
        goto cleanup;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto cleanup;

    size_t request_cap = body.output_len + 1024;
    request = malloc(request_cap);
    if (!request)
        goto cleanup;

    int written = snprintf(request, request_cap,
                           "NOTIFY %s HTTP/1.1\r\n"
                           "HOST: %s:%u\r\n"
                           "CONTENT-TYPE: text/xml; charset=\"utf-8\"\r\n"
                           "CONTENT-LENGTH: %zu\r\n"
                           "SERVER: %s\r\n"
                           "NT: upnp:event\r\n"
                           "NTS: upnp:propchange\r\n"
                           "SID: %s\r\n"
                           "SEQ: %u\r\n"
                           "CONNECTION: close\r\n"
                           "\r\n"
                           "%s",
                           target->callback_path,
                           target->callback_host,
                           (unsigned)target->callback_port,
                           body.output_len,
                           DLNA_SERVER_INFO,
                           target->sid,
                           target->seq,
                           body.output_xml ? body.output_xml : "");
    if (written < 0 || (size_t)written >= request_cap)
        goto cleanup;

    success = event_send_all(sock, request, (size_t)written);
    if (success)
    {
        log_info("[event] notify service=%s sid=%s seq=%u callback=http://%s:%u%s bytes=%zu\n",
                 event_service_name(service),
                 target->sid,
                 target->seq,
                 target->callback_host,
                 (unsigned)target->callback_port,
                 target->callback_path,
                 body.output_len);
    }
    else
    {
        log_warn("[event] notify failed service=%s sid=%s seq=%u callback=http://%s:%u%s errno=%d\n",
                 event_service_name(service),
                 target->sid,
                 target->seq,
                 target->callback_host,
                 (unsigned)target->callback_port,
                 target->callback_path,
                 errno);
    }

cleanup:
    if (sock >= 0)
        close(sock);
    free(request);
    soap_writer_dispose(&body);
    return success;
}

static size_t event_collect_targets_locked(EventService service, EventNotifyTarget *targets, size_t max_targets)
{
    size_t count = 0;
    uint64_t now_ms = event_now_ms();

    event_prune_expired_locked(now_ms);

    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS && count < max_targets; ++i)
    {
        EventSubscription *subscription = &g_event_subscriptions[i];
        if (!subscription->active || subscription->service != service)
            continue;

        targets[count].in_use = true;
        targets[count].service = service;
        targets[count].sid = subscription->sid ? strdup(subscription->sid) : NULL;
        targets[count].callback_host = subscription->callback_host ? strdup(subscription->callback_host) : NULL;
        targets[count].callback_port = subscription->callback_port;
        targets[count].callback_path = subscription->callback_path ? strdup(subscription->callback_path) : NULL;
        targets[count].seq = subscription->seq++;
        if ((subscription->sid && !targets[count].sid) ||
            (subscription->callback_host && !targets[count].callback_host) ||
            (subscription->callback_path && !targets[count].callback_path))
        {
            event_notify_target_clear(&targets[count]);
            continue;
        }
        ++count;
    }

    return count;
}

static void event_thread_main(void *arg)
{
    (void)arg;

    while (true)
    {
        bool avt_due = false;
        bool rc_due = false;
        bool cm_due = false;
        EventNotifyTarget avt_targets[EVENT_MAX_SUBSCRIPTIONS];
        EventNotifyTarget rc_targets[EVENT_MAX_SUBSCRIPTIONS];
        EventNotifyTarget cm_targets[EVENT_MAX_SUBSCRIPTIONS];
        size_t avt_count = 0;
        size_t rc_count = 0;
        size_t cm_count = 0;

        memset(avt_targets, 0, sizeof(avt_targets));
        memset(rc_targets, 0, sizeof(rc_targets));
        memset(cm_targets, 0, sizeof(cm_targets));

        mutexLock(&g_event_mutex);
        while (!g_event_stop_requested &&
               !g_avtransport_state_dirty &&
               !g_renderingcontrol_dirty &&
               !g_connectionmanager_dirty)
        {
            condvarWait(&g_event_cond, &g_event_mutex);
        }

        if (g_event_stop_requested)
        {
            mutexUnlock(&g_event_mutex);
            break;
        }

        if (g_avtransport_state_dirty)
            avt_due = true;

        rc_due = g_renderingcontrol_dirty;
        cm_due = g_connectionmanager_dirty;

        if (avt_due)
        {
            avt_count = event_collect_targets_locked(EVENT_SERVICE_AVTRANSPORT, avt_targets, EVENT_MAX_SUBSCRIPTIONS);
            g_avtransport_state_dirty = false;
        }

        if (rc_due)
        {
            rc_count = event_collect_targets_locked(EVENT_SERVICE_RENDERINGCONTROL, rc_targets, EVENT_MAX_SUBSCRIPTIONS);
            g_renderingcontrol_dirty = false;
        }

        if (cm_due)
        {
            cm_count = event_collect_targets_locked(EVENT_SERVICE_CONNECTIONMANAGER, cm_targets, EVENT_MAX_SUBSCRIPTIONS);
            g_connectionmanager_dirty = false;
        }

        mutexUnlock(&g_event_mutex);

        if (!avt_due && !rc_due && !cm_due)
        {
            svcSleepThread((int64_t)EVENT_NOTIFY_RETRY_SLEEP_MS * 1000000LL);
            continue;
        }

        for (size_t i = 0; i < avt_count; ++i)
            (void)event_send_notify(&avt_targets[i], EVENT_SERVICE_AVTRANSPORT);
        for (size_t i = 0; i < rc_count; ++i)
            (void)event_send_notify(&rc_targets[i], EVENT_SERVICE_RENDERINGCONTROL);
        for (size_t i = 0; i < cm_count; ++i)
            (void)event_send_notify(&cm_targets[i], EVENT_SERVICE_CONNECTIONMANAGER);

        event_clear_targets(avt_targets, avt_count);
        event_clear_targets(rc_targets, rc_count);
        event_clear_targets(cm_targets, cm_count);
    }

    threadExit();
}

bool event_server_start(void)
{
    Result rc;

    event_ensure_sync();

    mutexLock(&g_event_mutex);
    if (g_event_thread_started)
    {
        mutexUnlock(&g_event_mutex);
        return true;
    }

    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS; ++i)
        event_subscription_clear(&g_event_subscriptions[i]);
    g_event_stop_requested = false;
    g_avtransport_state_dirty = false;
    g_renderingcontrol_dirty = false;
    g_connectionmanager_dirty = false;
    mutexUnlock(&g_event_mutex);

    rc = threadCreate(&g_event_thread,
                      event_thread_main,
                      NULL,
                      NULL,
                      EVENT_THREAD_STACK_SIZE,
                      0x2C,
                      -2);
    if (R_FAILED(rc))
        return false;

    rc = threadStart(&g_event_thread);
    if (R_FAILED(rc))
    {
        threadClose(&g_event_thread);
        return false;
    }

    mutexLock(&g_event_mutex);
    g_event_thread_started = true;
    mutexUnlock(&g_event_mutex);
    log_info("[event] DLNA event server started.\n");
    return true;
}

void event_server_stop(void)
{
    log_info("[event] stop begin sync_pending=1\n");
    event_ensure_sync();

    mutexLock(&g_event_mutex);
    if (!g_event_thread_started)
    {
        mutexUnlock(&g_event_mutex);
        log_info("[event] stop skip reason=not-started\n");
        return;
    }

    g_event_stop_requested = true;
    condvarWakeAll(&g_event_cond);
    mutexUnlock(&g_event_mutex);

    log_info("[event] stop waiting for thread exit\n");
    threadWaitForExit(&g_event_thread);
    threadClose(&g_event_thread);

    mutexLock(&g_event_mutex);
    g_event_thread_started = false;
    for (size_t i = 0; i < EVENT_MAX_SUBSCRIPTIONS; ++i)
        event_subscription_clear(&g_event_subscriptions[i]);
    g_avtransport_state_dirty = false;
    g_renderingcontrol_dirty = false;
    g_connectionmanager_dirty = false;
    mutexUnlock(&g_event_mutex);
    log_info("[event] stop thread closed and subscriptions cleared\n");
    log_info("[event] DLNA event server stopped.\n");
}

static bool event_handle_subscribe(const HttpRequestContext *ctx,
                                   EventService service,
                                   char *response,
                                   size_t response_size,
                                   size_t *response_len)
{
    char *sid = NULL;
    char *callback = NULL;
    char *host = NULL;
    char *path = NULL;
    char *nt = NULL;
    int timeout_seconds;
    char *headers = NULL;
    uint64_t expiry_ms;
    uint16_t callback_port = 0;

    if (!ctx || !ctx->request || !response || !response_len)
        return false;

    if (!event_parse_timeout_seconds(ctx->request, &timeout_seconds))
        return event_build_response(400, "Bad Request", NULL, "", response, response_size, response_len);

    if (event_get_header_value_alloc(ctx->request, "SID", &sid))
    {
        ssize_t slot_index;

        mutexLock(&g_event_mutex);
        slot_index = event_find_subscription_by_sid_locked(sid);
        if (slot_index < 0)
        {
            mutexUnlock(&g_event_mutex);
            free(sid);
            return event_build_response(412, "Precondition Failed", NULL, "", response, response_size, response_len);
        }

        expiry_ms = event_now_ms() + (uint64_t)timeout_seconds * 1000ULL;
        g_event_subscriptions[slot_index].expiry_ms = expiry_ms;
        headers = soap_handler_strdup_printf("SID: %s\r\nTIMEOUT: Second-%d\r\n",
                                             g_event_subscriptions[slot_index].sid,
                                             timeout_seconds);
        mutexUnlock(&g_event_mutex);

        log_info("[event] renew service=%s sid=%s timeout=%d\n",
                 event_service_name(service), sid, timeout_seconds);
        bool built = headers && event_build_response(200, "OK", headers, "", response, response_size, response_len);
        free(headers);
        free(sid);
        return built;
    }

    if (!event_get_header_value_alloc(ctx->request, "CALLBACK", &callback) ||
        !event_get_header_value_alloc(ctx->request, "NT", &nt))
    {
        free(callback);
        free(nt);
        return event_build_response(400, "Bad Request", NULL, "", response, response_size, response_len);
    }

    char *normalized_callback = NULL;
    if (strcasecmp(nt, "upnp:event") != 0 ||
        !event_parse_callback_url(callback, &normalized_callback, &host, &callback_port, &path))
    {
        free(callback);
        free(normalized_callback);
        free(host);
        free(path);
        free(nt);
        return event_build_response(412, "Precondition Failed", NULL, "", response, response_size, response_len);
    }
    free(callback);
    callback = normalized_callback;

    mutexLock(&g_event_mutex);
    ssize_t slot_index = event_find_free_subscription_locked();
    if (slot_index < 0)
    {
        mutexUnlock(&g_event_mutex);
        free(callback);
        free(host);
        free(path);
        free(nt);
        return event_build_response(503, "Service Unavailable", NULL, "", response, response_size, response_len);
    }

    expiry_ms = event_now_ms() + (uint64_t)timeout_seconds * 1000ULL;
    EventSubscription *subscription = &g_event_subscriptions[slot_index];
    event_subscription_clear(subscription);
    subscription->active = true;
    subscription->service = service;
    subscription->sid = soap_handler_strdup_printf("uuid:nxcast-evt-%08u", g_next_sid++);
    subscription->callback_url = callback ? strdup(callback) : NULL;
    subscription->callback_host = host ? strdup(host) : NULL;
    subscription->callback_port = callback_port;
    subscription->callback_path = path ? strdup(path) : NULL;
    subscription->seq = 0;
    subscription->expiry_ms = expiry_ms;
    if (!subscription->sid || !subscription->callback_url || !subscription->callback_host || !subscription->callback_path)
    {
        event_subscription_clear(subscription);
        mutexUnlock(&g_event_mutex);
        free(callback);
        free(host);
        free(path);
        free(nt);
        return event_build_response(503, "Service Unavailable", NULL, "", response, response_size, response_len);
    }
    event_mark_dirty_locked(service);
    condvarWakeOne(&g_event_cond);
    headers = soap_handler_strdup_printf("SID: %s\r\nTIMEOUT: Second-%d\r\n",
                                         subscription->sid,
                                         timeout_seconds);
    log_info("[event] subscribe service=%s sid=%s callback=%s timeout=%d\n",
             event_service_name(service),
             subscription->sid,
             subscription->callback_url,
             timeout_seconds);
    mutexUnlock(&g_event_mutex);

    free(callback);
    free(host);
    free(path);
    free(nt);
    if (!headers)
        return false;
    bool built = event_build_response(200, "OK", headers, "", response, response_size, response_len);
    free(headers);
    return built;
}

static bool event_handle_unsubscribe(const HttpRequestContext *ctx,
                                     EventService service,
                                     char *response,
                                     size_t response_size,
                                     size_t *response_len)
{
    char *sid = NULL;
    ssize_t slot_index;

    (void)service;

    if (!ctx || !ctx->request || !response || !response_len)
        return false;

    if (!event_get_header_value_alloc(ctx->request, "SID", &sid))
        return event_build_response(400, "Bad Request", NULL, "", response, response_size, response_len);

    mutexLock(&g_event_mutex);
    slot_index = event_find_subscription_by_sid_locked(sid);
    if (slot_index < 0)
    {
        mutexUnlock(&g_event_mutex);
        free(sid);
        return event_build_response(412, "Precondition Failed", NULL, "", response, response_size, response_len);
    }

    log_info("[event] unsubscribe service=%s sid=%s callback=%s\n",
             event_service_name(g_event_subscriptions[slot_index].service),
             g_event_subscriptions[slot_index].sid,
             g_event_subscriptions[slot_index].callback_url);
    event_subscription_clear(&g_event_subscriptions[slot_index]);
    mutexUnlock(&g_event_mutex);

    free(sid);
    return event_build_response(200, "OK", NULL, "", response, response_size, response_len);
}

bool event_server_try_handle_http(const HttpRequestContext *ctx,
                                  char *response,
                                  size_t response_size,
                                  size_t *response_len)
{
    EventService service;

    if (!ctx || !ctx->path || !response || !response_len)
        return false;

    *response_len = 0;
    service = event_service_from_path(ctx->path);
    if (service == EVENT_SERVICE_INVALID)
        return false;

    if (strcasecmp(ctx->method, "SUBSCRIBE") == 0)
        return event_handle_subscribe(ctx, service, response, response_size, response_len);
    if (strcasecmp(ctx->method, "UNSUBSCRIBE") == 0)
        return event_handle_unsubscribe(ctx, service, response, response_size, response_len);

    return event_build_response(405, "Method Not Allowed", NULL, "", response, response_size, response_len);
}

void event_server_on_renderer_event(const RendererEvent *event)
{
    if (!event)
        return;

    event_ensure_sync();
    mutexLock(&g_event_mutex);
    switch (event->type)
    {
    case PLAYER_EVENT_STATE_CHANGED:
    case PLAYER_EVENT_URI_CHANGED:
    case PLAYER_EVENT_DURATION_CHANGED:
    case PLAYER_EVENT_ERROR:
        event_mark_dirty_locked(EVENT_SERVICE_AVTRANSPORT);
        break;
    case PLAYER_EVENT_POSITION_CHANGED:
        break;
    case PLAYER_EVENT_VOLUME_CHANGED:
    case PLAYER_EVENT_MUTE_CHANGED:
        event_mark_dirty_locked(EVENT_SERVICE_RENDERINGCONTROL);
        break;
    default:
        break;
    }
    condvarWakeOne(&g_event_cond);
    mutexUnlock(&g_event_mutex);
}
