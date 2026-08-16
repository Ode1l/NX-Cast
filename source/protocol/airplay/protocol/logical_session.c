#include "logical_session.h"

#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex AirPlaySessionMutex;
#else
#include <pthread.h>
typedef pthread_mutex_t AirPlaySessionMutex;
#endif

typedef struct
{
    uint64_t connection_id;
    uint64_t logical_session_id;
    AirPlayConnectionKind kind;
    bool logical_session_bound;
    bool used;
} AirPlayConnectionEntry;

typedef struct
{
    char apple_session_id[AIRPLAY_SESSION_APPLE_ID_MAX + 1U];
    uint64_t logical_session_id;
    uint32_t connection_count;
    bool used;
} AirPlayLogicalSessionEntry;

struct AirPlaySessionManager
{
    AirPlaySessionMutex mutex;
    AirPlayConnectionEntry connections[AIRPLAY_SESSION_MAX_CONNECTIONS];
    AirPlayLogicalSessionEntry logical_sessions[AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS];
    uint64_t next_logical_session_id;
    bool mutex_ready;
};

static bool session_mutex_init(AirPlaySessionMutex *mutex)
{
#ifdef __SWITCH__
    mutexInit(mutex);
    return true;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void session_mutex_destroy(AirPlaySessionMutex *mutex)
{
#ifndef __SWITCH__
    pthread_mutex_destroy(mutex);
#else
    (void)mutex;
#endif
}

static void session_mutex_lock(AirPlaySessionMutex *mutex)
{
#ifdef __SWITCH__
    mutexLock(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void session_mutex_unlock(AirPlaySessionMutex *mutex)
{
#ifdef __SWITCH__
    mutexUnlock(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

static size_t bounded_length(const char *value, size_t maximum)
{
    size_t length = 0U;

    if (!value)
        return 0U;
    while (length <= maximum && value[length] != '\0')
        length++;
    return length;
}

static bool valid_apple_session_id(const char *value)
{
    size_t index;
    size_t length = bounded_length(value, AIRPLAY_SESSION_APPLE_ID_MAX);

    if (length == 0U || length > AIRPLAY_SESSION_APPLE_ID_MAX)
        return false;
    for (index = 0U; index < length; ++index)
    {
        unsigned char character = (unsigned char)value[index];
        if (character < 0x21U || character > 0x7eU)
            return false;
    }
    return true;
}

static bool uri_is_ble_info(const char *uri)
{
    return uri && (strstr(uri, "txtAirPlay") || strstr(uri, "txtRAOP"));
}

static bool uri_is_pairing(const char *uri)
{
    return uri && (strcmp(uri, "/pair-pin-start") == 0 ||
                   strcmp(uri, "/pair-setup-pin") == 0 ||
                   strcmp(uri, "/pair-setup") == 0 ||
                   strcmp(uri, "/pair-verify") == 0);
}

bool airplay_session_request_is_remote_video(const AirPlayRtspRequest *request)
{
    const char *uri;

    if (!request)
        return false;
    uri = request->uri;
    return strcmp(uri, "/play") == 0 ||
           strcmp(uri, "/stop") == 0 ||
           strcmp(uri, "/playback-info") == 0 ||
           strcmp(uri, "/action") == 0 ||
           strcmp(uri, "/rate") == 0 ||
           strncmp(uri, "/rate?", 6U) == 0 ||
           strcmp(uri, "/scrub") == 0 ||
           strncmp(uri, "/scrub?", 7U) == 0;
}

bool airplay_session_request_requires_logical_binding(
    const AirPlayRtspRequest *request)
{
    return request &&
           (airplay_session_request_is_remote_video(request) ||
            strcmp(request->uri, "/reverse") == 0);
}

static AirPlayConnectionKind classify_request(const AirPlayRtspRequest *request,
                                              const char *apple_session_id)
{
    if (!request)
        return AIRPLAY_CONNECTION_UNKNOWN;
    if (uri_is_ble_info(request->uri) && !request->has_cseq &&
        strcmp(request->protocol, "RTSP/1.0") == 0)
    {
        return AIRPLAY_CONNECTION_BLE;
    }
    if (request->has_cseq)
        return AIRPLAY_CONNECTION_RAOP;
    if (apple_session_id || strcmp(request->protocol, "HTTP/1.1") == 0 ||
        uri_is_pairing(request->uri))
    {
        return AIRPLAY_CONNECTION_AIRPLAY;
    }
    return AIRPLAY_CONNECTION_UNKNOWN;
}

static AirPlayConnectionEntry *find_connection(AirPlaySessionManager *manager,
                                               uint64_t connection_id)
{
    size_t index;

    for (index = 0U; index < AIRPLAY_SESSION_MAX_CONNECTIONS; ++index)
    {
        AirPlayConnectionEntry *entry = &manager->connections[index];
        if (entry->used && entry->connection_id == connection_id)
            return entry;
    }
    return NULL;
}

static AirPlayConnectionEntry *find_free_connection(AirPlaySessionManager *manager)
{
    size_t index;

    for (index = 0U; index < AIRPLAY_SESSION_MAX_CONNECTIONS; ++index)
    {
        if (!manager->connections[index].used)
            return &manager->connections[index];
    }
    return NULL;
}

static AirPlayLogicalSessionEntry *find_logical_by_apple_id(
    AirPlaySessionManager *manager,
    const char *apple_session_id)
{
    size_t index;

    for (index = 0U; index < AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS; ++index)
    {
        AirPlayLogicalSessionEntry *entry = &manager->logical_sessions[index];
        if (entry->used && strcmp(entry->apple_session_id, apple_session_id) == 0)
            return entry;
    }
    return NULL;
}

static AirPlayLogicalSessionEntry *find_logical_by_id(AirPlaySessionManager *manager,
                                                      uint64_t logical_session_id)
{
    size_t index;

    for (index = 0U; index < AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS; ++index)
    {
        AirPlayLogicalSessionEntry *entry = &manager->logical_sessions[index];
        if (entry->used && entry->logical_session_id == logical_session_id)
            return entry;
    }
    return NULL;
}

static AirPlayLogicalSessionEntry *find_free_logical(AirPlaySessionManager *manager)
{
    size_t index;

    for (index = 0U; index < AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS; ++index)
    {
        if (!manager->logical_sessions[index].used)
            return &manager->logical_sessions[index];
    }
    return NULL;
}

static uint64_t allocate_logical_id(AirPlaySessionManager *manager)
{
    uint64_t candidate;
    size_t attempts;

    for (attempts = 0U; attempts <= AIRPLAY_SESSION_MAX_LOGICAL_SESSIONS; ++attempts)
    {
        candidate = manager->next_logical_session_id++;
        if (manager->next_logical_session_id < UINT64_C(0x8000000000000000))
            manager->next_logical_session_id = UINT64_C(0x8000000000000000);
        if (candidate != 0U && !find_logical_by_id(manager, candidate))
            return candidate;
    }
    return 0U;
}

static void fill_snapshot(const AirPlayConnectionEntry *connection,
                          const AirPlayLogicalSessionEntry *logical,
                          AirPlaySessionSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->connection_id = connection->connection_id;
    snapshot->logical_session_id = connection->logical_session_id;
    snapshot->connection_kind = connection->kind;
    snapshot->logical_session_bound = connection->logical_session_bound;
    snapshot->logical_connection_count = logical ? logical->connection_count : 1U;
}

AirPlaySessionManager *airplay_session_manager_create(void)
{
    AirPlaySessionManager *manager = calloc(1U, sizeof(*manager));

    if (!manager)
        return NULL;
    if (!session_mutex_init(&manager->mutex))
    {
        free(manager);
        return NULL;
    }
    manager->mutex_ready = true;
    manager->next_logical_session_id = UINT64_C(0x8000000000000000);
    return manager;
}

void airplay_session_manager_destroy(AirPlaySessionManager *manager)
{
    if (!manager)
        return;
    if (manager->mutex_ready)
        session_mutex_destroy(&manager->mutex);
    memset(manager, 0, sizeof(*manager));
    free(manager);
}

AirPlaySessionObserveResult airplay_session_manager_observe(
    AirPlaySessionManager *manager,
    uint64_t connection_id,
    const AirPlayRtspRequest *request,
    AirPlaySessionSnapshot *snapshot_out)
{
    const char *apple_session_id;
    AirPlayConnectionKind observed_kind;
    AirPlayConnectionEntry *connection;
    AirPlayLogicalSessionEntry *logical = NULL;
    bool new_connection;

    if (!manager || !connection_id || !request || !snapshot_out)
        return AIRPLAY_SESSION_OBSERVE_INVALID_ARGUMENT;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    apple_session_id = airplay_rtsp_request_header(request, "X-Apple-Session-ID");
    if (apple_session_id && !valid_apple_session_id(apple_session_id))
        return AIRPLAY_SESSION_OBSERVE_INVALID_SESSION_ID;
    if (request->has_cseq && apple_session_id)
        return AIRPLAY_SESSION_OBSERVE_CONFLICT;
    observed_kind = classify_request(request, apple_session_id);

    session_mutex_lock(&manager->mutex);
    connection = find_connection(manager, connection_id);
    new_connection = connection == NULL;
    if (new_connection)
    {
        connection = find_free_connection(manager);
        if (!connection)
        {
            session_mutex_unlock(&manager->mutex);
            return AIRPLAY_SESSION_OBSERVE_CAPACITY;
        }
    }
    else if (observed_kind != AIRPLAY_CONNECTION_UNKNOWN &&
             connection->kind != AIRPLAY_CONNECTION_UNKNOWN &&
             connection->kind != observed_kind)
    {
        session_mutex_unlock(&manager->mutex);
        return AIRPLAY_SESSION_OBSERVE_CONFLICT;
    }

    if (apple_session_id && connection->logical_session_bound)
    {
        logical = find_logical_by_id(manager, connection->logical_session_id);
        if (!logical || strcmp(logical->apple_session_id, apple_session_id) != 0)
        {
            session_mutex_unlock(&manager->mutex);
            return AIRPLAY_SESSION_OBSERVE_CONFLICT;
        }
    }
    else if (apple_session_id)
    {
        logical = find_logical_by_apple_id(manager, apple_session_id);
        if (!logical)
        {
            logical = find_free_logical(manager);
            if (!logical)
            {
                session_mutex_unlock(&manager->mutex);
                return AIRPLAY_SESSION_OBSERVE_CAPACITY;
            }
        }
    }
    else if (connection->logical_session_bound)
    {
        logical = find_logical_by_id(manager, connection->logical_session_id);
        if (!logical)
        {
            session_mutex_unlock(&manager->mutex);
            return AIRPLAY_SESSION_OBSERVE_CONFLICT;
        }
    }

    if (airplay_session_request_requires_logical_binding(request) && !logical)
    {
        session_mutex_unlock(&manager->mutex);
        return AIRPLAY_SESSION_OBSERVE_UNBOUND_MEDIA;
    }

    if (new_connection)
    {
        memset(connection, 0, sizeof(*connection));
        connection->used = true;
        connection->connection_id = connection_id;
        connection->logical_session_id = connection_id;
        connection->kind = observed_kind;
    }
    else if (connection->kind == AIRPLAY_CONNECTION_UNKNOWN)
    {
        connection->kind = observed_kind;
    }

    if (apple_session_id && !connection->logical_session_bound)
    {
        if (!logical->used)
        {
            memset(logical, 0, sizeof(*logical));
            logical->logical_session_id = allocate_logical_id(manager);
            if (!logical->logical_session_id)
            {
                if (new_connection)
                    memset(connection, 0, sizeof(*connection));
                session_mutex_unlock(&manager->mutex);
                return AIRPLAY_SESSION_OBSERVE_CAPACITY;
            }
            memcpy(logical->apple_session_id,
                   apple_session_id,
                   strlen(apple_session_id) + 1U);
            logical->used = true;
        }
        logical->connection_count++;
        connection->logical_session_id = logical->logical_session_id;
        connection->logical_session_bound = true;
    }

    fill_snapshot(connection, logical, snapshot_out);
    session_mutex_unlock(&manager->mutex);
    return AIRPLAY_SESSION_OBSERVE_OK;
}

bool airplay_session_manager_close(AirPlaySessionManager *manager,
                                   uint64_t connection_id,
                                   AirPlaySessionCloseResult *result_out)
{
    AirPlayConnectionEntry *connection;
    AirPlayLogicalSessionEntry *logical = NULL;

    if (!manager || !connection_id || !result_out)
        return false;
    memset(result_out, 0, sizeof(*result_out));
    session_mutex_lock(&manager->mutex);
    connection = find_connection(manager, connection_id);
    if (!connection)
    {
        session_mutex_unlock(&manager->mutex);
        return false;
    }
    result_out->connection_id = connection_id;
    result_out->logical_session_id = connection->logical_session_id;
    result_out->logical_session_bound = connection->logical_session_bound;
    if (connection->logical_session_bound)
    {
        logical = find_logical_by_id(manager, connection->logical_session_id);
        if (logical && logical->connection_count > 0U)
        {
            logical->connection_count--;
            if (logical->connection_count == 0U)
            {
                result_out->last_logical_connection = true;
                memset(logical, 0, sizeof(*logical));
            }
        }
    }
    memset(connection, 0, sizeof(*connection));
    session_mutex_unlock(&manager->mutex);
    return true;
}

const char *airplay_connection_kind_name(AirPlayConnectionKind kind)
{
    switch (kind)
    {
    case AIRPLAY_CONNECTION_UNKNOWN:
        return "unknown";
    case AIRPLAY_CONNECTION_RAOP:
        return "raop";
    case AIRPLAY_CONNECTION_AIRPLAY:
        return "airplay";
    case AIRPLAY_CONNECTION_BLE:
        return "ble";
    default:
        return "invalid";
    }
}

const char *airplay_session_observe_result_name(AirPlaySessionObserveResult result)
{
    switch (result)
    {
    case AIRPLAY_SESSION_OBSERVE_OK:
        return "ok";
    case AIRPLAY_SESSION_OBSERVE_INVALID_ARGUMENT:
        return "invalid-argument";
    case AIRPLAY_SESSION_OBSERVE_INVALID_SESSION_ID:
        return "invalid-session-id";
    case AIRPLAY_SESSION_OBSERVE_CONFLICT:
        return "conflict";
    case AIRPLAY_SESSION_OBSERVE_UNBOUND_MEDIA:
        return "unbound-media";
    case AIRPLAY_SESSION_OBSERVE_CAPACITY:
        return "capacity";
    default:
        return "invalid";
    }
}
