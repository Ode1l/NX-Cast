#include "protocol_coordinator.h"

#include <string.h>

#include "runtime_observability.h"

#ifdef __SWITCH__
#include <switch.h>
#include "log/log.h"
typedef Mutex ProtocolCoordinatorMutex;
#define COORDINATOR_LOG_INFO(...) log_info(__VA_ARGS__)
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t ProtocolCoordinatorMutex;
#define COORDINATOR_LOG_INFO(...) ((void)0)
#endif

#define PROTOCOL_SERVICE_WORKER_STACK_SIZE 0x40000u
#define PROTOCOL_SERVICE_WORKER_PRIORITY 0x2bu
#define PROTOCOL_SERVICE_AIRPLAY_WORKER_PRIORITY 0x2eu
#define PROTOCOL_RESOURCE_WORKER_STACK_SIZE 0x40000u
#define PROTOCOL_RESOURCE_WORKER_PRIORITY 0x2du
#define PROTOCOL_RESOURCE_RETRY_MS 1000u
#define PROTOCOL_TERMINAL_RELEASE_GRACE_MS 1000u

typedef enum
{
    PROTOCOL_LIFECYCLE_STOPPED = 0,
    PROTOCOL_LIFECYCLE_STARTING,
    PROTOCOL_LIFECYCLE_RUNNING,
    PROTOCOL_LIFECYCLE_STOPPING
} ProtocolLifecycle;

typedef struct
{
    ProtocolService service;
    bool thread_started;
    bool start_finished;
    bool start_result;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
} ProtocolServiceWorker;

typedef struct
{
    bool thread_started;
    bool finished;
    bool result;
    ProtocolResourceMode target;
    ProtocolService current_service;
    bool start_call_active;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
} ProtocolResourceWorker;

typedef struct
{
    ProtocolCoordinatorMutex mutex;
    bool mutex_ready;
    ProtocolLifecycle lifecycle;
    bool runtime_active;
    bool serial_startup;
    bool exclusive_media_resources;
    bool configured_enabled[PROTOCOL_SERVICE_COUNT];
    bool background_network_suspended;
    ProtocolDiscoverySuspensionPolicy discovery_suspension_policy;
    int next_service_to_start;
    bool stop_required[PROTOCOL_SERVICE_COUNT];
    ProtocolServiceWorker workers[PROTOCOL_SERVICE_COUNT];
    ProtocolResourceWorker resource_worker;
    uint64_t service_transition_ms[PROTOCOL_SERVICE_COUNT];
    uint64_t resource_transition_ms;
    uint64_t resource_retry_due_ms;
    uint64_t media_claim_ms;
    uint64_t media_ready_ms;
    uint32_t playback_observed_generation;
    bool playback_seen_active;
    ProtocolCoordinatorOperations operations;
    ProtocolCoordinatorSnapshot snapshot;
} ProtocolCoordinator;

static ProtocolCoordinator g_coordinator;

static void coordinator_service_worker_run(ProtocolServiceWorker *worker);
static bool coordinator_launch_next_serial_worker(void);
static void coordinator_resource_worker_run(ProtocolResourceWorker *worker);
static bool coordinator_launch_resource_worker(void);
static bool coordinator_resource_services_match_locked(
    ProtocolResourceMode mode);

static uint64_t coordinator_monotonic_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
#else
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000ULL +
           (uint64_t)now.tv_nsec / 1000000ULL;
#endif
}

static void coordinator_init(void)
{
    if (g_coordinator.mutex_ready)
        return;
#ifdef __SWITCH__
    mutexInit(&g_coordinator.mutex);
#else
    (void)pthread_mutex_init(&g_coordinator.mutex, NULL);
#endif
    g_coordinator.mutex_ready = true;
}

static void coordinator_lock(void)
{
    coordinator_init();
#ifdef __SWITCH__
    mutexLock(&g_coordinator.mutex);
#else
    pthread_mutex_lock(&g_coordinator.mutex);
#endif
}

static void coordinator_unlock(void)
{
#ifdef __SWITCH__
    mutexUnlock(&g_coordinator.mutex);
#else
    pthread_mutex_unlock(&g_coordinator.mutex);
#endif
}

static void coordinator_bump_revision(void)
{
    ++g_coordinator.snapshot.revision;
    if (g_coordinator.snapshot.revision == 0u)
        ++g_coordinator.snapshot.revision;
}

static bool service_valid(ProtocolService service)
{
    return service >= PROTOCOL_SERVICE_IPTV && service < PROTOCOL_SERVICE_COUNT;
}

static bool service_state_valid(ProtocolServiceState state)
{
    return state >= PROTOCOL_SERVICE_DISABLED && state <= PROTOCOL_SERVICE_FAILED;
}

static void coordinator_sleep_ms(uint32_t duration_ms)
{
#ifdef __SWITCH__
    svcSleepThread((int64_t)duration_ms * 1000000LL);
#else
    struct timespec duration = {
        .tv_sec = (time_t)(duration_ms / 1000u),
        .tv_nsec = (long)(duration_ms % 1000u) * 1000000L,
    };

    while (nanosleep(&duration, &duration) != 0)
    {
    }
#endif
}

static bool resource_mode_valid(ProtocolResourceMode mode)
{
    return mode >= PROTOCOL_RESOURCE_MODE_HOME &&
           mode <= PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE;
}

static ProtocolResourceMode resource_mode_for_owner(PlayerMediaOwner owner)
{
    switch (owner)
    {
    case PLAYER_MEDIA_OWNER_IPTV:
        return PROTOCOL_RESOURCE_MODE_IPTV_EXCLUSIVE;
    case PLAYER_MEDIA_OWNER_DLNA:
        return PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE;
    case PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO:
    case PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR:
        return PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE;
    case PLAYER_MEDIA_OWNER_NONE:
    default:
        return PROTOCOL_RESOURCE_MODE_HOME;
    }
}

static bool discovery_suspension_policy_valid(
    ProtocolDiscoverySuspensionPolicy policy)
{
    return policy >= PROTOCOL_DISCOVERY_SUSPEND_NONE &&
           policy <= PROTOCOL_DISCOVERY_SUSPEND_PLAYBACK_ACTIVITY;
}

static bool operations_valid(const ProtocolCoordinatorConfig *config,
                             const ProtocolCoordinatorOperations *operations)
{
    int index;

    if (!config || !operations ||
        !discovery_suspension_policy_valid(
            config->discovery_suspension_policy))
        return false;
    if (config->discovery_suspension_policy !=
            PROTOCOL_DISCOVERY_SUSPEND_NONE &&
        !operations->set_discovery_suspended)
        return false;
    if (config->exclusive_media_resources &&
        !operations->set_background_network_suspended)
        return false;
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (config->enabled[index] &&
            (!operations->services[index].start ||
             !operations->services[index].stop))
            return false;
    }
    if (config->enabled[PROTOCOL_SERVICE_AIRPLAY] &&
        !operations->airplay_get_status)
        return false;
    return true;
}

static void coordinator_recompute_state(void)
{
    bool has_running = false;
    bool has_starting = false;
    bool has_failed = false;
    int index;

    if (g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPED)
    {
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_STOPPED;
        return;
    }
    if (g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPING)
    {
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_STOPPING;
        return;
    }

    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        ProtocolServiceState state = g_coordinator.snapshot.services[index];

        has_running = has_running || state == PROTOCOL_SERVICE_RUNNING;
        has_starting = has_starting || state == PROTOCOL_SERVICE_STARTING;
        has_failed = has_failed || state == PROTOCOL_SERVICE_FAILED;
    }

    if (g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STARTING && has_starting)
    {
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_STARTING;
        return;
    }

    g_coordinator.lifecycle = PROTOCOL_LIFECYCLE_RUNNING;
    if (g_coordinator.snapshot.active_media.owner != PLAYER_MEDIA_OWNER_NONE)
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_MEDIA_ACTIVE;
    else if (has_running)
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_READY;
    else if (has_failed)
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_FAILED;
    else
        g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_READY;
}

static bool coordinator_accepts_media(void)
{
    bool accepts;

    coordinator_lock();
    accepts = g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STARTING ||
              g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_RUNNING;
    coordinator_unlock();
    return accepts;
}

static bool coordinator_accepts_guard(bool allow_stopping)
{
    bool accepts;

    coordinator_lock();
    accepts = g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STARTING ||
              g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_RUNNING ||
              (allow_stopping &&
               g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPING);
    coordinator_unlock();
    return accepts;
}

static bool coordinator_exclusive_resources_enabled(void)
{
    bool enabled;

    coordinator_lock();
    enabled = g_coordinator.exclusive_media_resources;
    coordinator_unlock();
    return enabled;
}

static void coordinator_set_active_media(const PlayerOwnershipLease *lease)
{
    coordinator_lock();
    if (lease)
    {
        if (g_coordinator.snapshot.active_media.generation != lease->generation)
        {
            g_coordinator.playback_observed_generation = lease->generation;
            g_coordinator.playback_seen_active = false;
            g_coordinator.media_claim_ms = coordinator_monotonic_ms();
            g_coordinator.media_ready_ms = 0u;
        }
        g_coordinator.snapshot.active_media = *lease;
    }
    else
    {
        memset(&g_coordinator.snapshot.active_media, 0,
               sizeof(g_coordinator.snapshot.active_media));
        g_coordinator.playback_observed_generation = 0u;
        g_coordinator.playback_seen_active = false;
        g_coordinator.media_claim_ms = 0u;
        g_coordinator.media_ready_ms = 0u;
    }
    coordinator_recompute_state();
    coordinator_bump_revision();
    coordinator_unlock();
}

static void coordinator_set_desired_resource_mode(ProtocolResourceMode mode)
{
    ProtocolResourceMode target;
    void (*request_stop)(void *context) = NULL;
    void *context = NULL;

    if (!resource_mode_valid(mode))
        mode = PROTOCOL_RESOURCE_MODE_HOME;
    coordinator_lock();
    target = g_coordinator.exclusive_media_resources
                 ? mode
                 : PROTOCOL_RESOURCE_MODE_HOME;
    if (g_coordinator.snapshot.desired_resource_mode != target)
    {
        g_coordinator.snapshot.desired_resource_mode = target;
        g_coordinator.snapshot.resource_transition_failed = false;
        g_coordinator.resource_retry_due_ms = 0u;
        if (g_coordinator.snapshot.resource_transition_active &&
            g_coordinator.resource_worker.target != target &&
            g_coordinator.resource_worker.start_call_active &&
            service_valid(g_coordinator.resource_worker.current_service))
        {
            request_stop =
                g_coordinator.operations
                    .services[g_coordinator.resource_worker.current_service]
                    .request_stop;
            context = g_coordinator.operations.context;
        }
        coordinator_bump_revision();
    }
    coordinator_unlock();
    if (request_stop)
        request_stop(context);
}

static bool coordinator_set_background_network_suspended(bool suspended)
{
    bool (*callback)(void *context, bool suspended) = NULL;
    void *context = NULL;
    bool changed = false;

    coordinator_lock();
    if (g_coordinator.background_network_suspended != suspended)
    {
        g_coordinator.background_network_suspended = suspended;
        callback = g_coordinator.operations.set_background_network_suspended;
        context = g_coordinator.operations.context;
        changed = true;
    }
    else if (suspended)
    {
        /* A failed quiesce is retried while the desired state stays true. */
        callback = g_coordinator.operations.set_background_network_suspended;
        context = g_coordinator.operations.context;
    }
    coordinator_unlock();
    if ((changed || suspended) && callback)
        return callback(context, suspended);
    return true;
}

static bool coordinator_resource_service_required(ProtocolResourceMode mode,
                                                  ProtocolService service,
                                                  const bool enabled[PROTOCOL_SERVICE_COUNT])
{
    if (!service_valid(service) || !enabled[service])
        return false;
    if (service == PROTOCOL_SERVICE_IPTV)
        return true;
    if (service == PROTOCOL_SERVICE_DLNA)
        return mode == PROTOCOL_RESOURCE_MODE_HOME ||
               mode == PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE;
    if (service == PROTOCOL_SERVICE_AIRPLAY)
        return mode == PROTOCOL_RESOURCE_MODE_HOME ||
               mode == PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE;
    return false;
}

static bool coordinator_resource_services_match_locked(
    ProtocolResourceMode mode)
{
    int index;

    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        ProtocolService service = (ProtocolService)index;
        bool required = coordinator_resource_service_required(
            mode, service, g_coordinator.configured_enabled);
        ProtocolServiceState state = g_coordinator.snapshot.services[index];

        if (!g_coordinator.configured_enabled[index])
        {
            if (state != PROTOCOL_SERVICE_DISABLED)
                return false;
        }
        else if (required)
        {
            if (state != PROTOCOL_SERVICE_RUNNING)
                return false;
        }
        else if (state != PROTOCOL_SERVICE_STOPPED)
        {
            return false;
        }
    }
    return true;
}

static void coordinator_apply_discovery_suspension(
    bool suspended, bool require_policy,
    ProtocolDiscoverySuspensionPolicy required_policy)
{
    void (*callback)(void *context, bool suspended) = NULL;
    void *context = NULL;
    bool target;
    bool changed = false;

    coordinator_lock();
    if (require_policy &&
        g_coordinator.discovery_suspension_policy != required_policy)
    {
        coordinator_unlock();
        return;
    }
    target = suspended && g_coordinator.runtime_active &&
             g_coordinator.operations.set_discovery_suspended != NULL;
    if (g_coordinator.snapshot.discovery_suspended != target)
    {
        g_coordinator.snapshot.discovery_suspended = target;
        callback = g_coordinator.operations.set_discovery_suspended;
        context = g_coordinator.operations.context;
        coordinator_bump_revision();
        changed = true;
    }
    coordinator_unlock();

    if (changed && callback)
        callback(context, target);
}

static void coordinator_set_discovery_suspended(bool suspended)
{
    coordinator_apply_discovery_suspension(
        suspended, false, PROTOCOL_DISCOVERY_SUSPEND_NONE);
}

static void coordinator_set_discovery_suspended_for_policy(
    ProtocolDiscoverySuspensionPolicy policy, bool suspended)
{
    coordinator_apply_discovery_suspension(suspended, true, policy);
}

static void coordinator_sync_active_media(void)
{
    PlayerOwnershipLease current = {0};
    bool has_current = player_ownership_current(&current);

    coordinator_lock();
    if ((!has_current &&
         g_coordinator.snapshot.active_media.owner != PLAYER_MEDIA_OWNER_NONE) ||
        (has_current &&
         (g_coordinator.snapshot.active_media.owner != current.owner ||
          g_coordinator.snapshot.active_media.token != current.token ||
          g_coordinator.snapshot.active_media.generation != current.generation)))
    {
        if (has_current)
        {
            if (g_coordinator.snapshot.active_media.generation !=
                current.generation)
            {
                g_coordinator.playback_observed_generation =
                    current.generation;
                g_coordinator.playback_seen_active = false;
                g_coordinator.media_claim_ms = coordinator_monotonic_ms();
                g_coordinator.media_ready_ms = 0u;
            }
            g_coordinator.snapshot.active_media = current;
        }
        else
        {
            memset(&g_coordinator.snapshot.active_media, 0,
                   sizeof(g_coordinator.snapshot.active_media));
            g_coordinator.playback_observed_generation = 0u;
            g_coordinator.playback_seen_active = false;
            g_coordinator.media_claim_ms = 0u;
            g_coordinator.media_ready_ms = 0u;
        }
        coordinator_recompute_state();
        coordinator_bump_revision();
    }
    coordinator_unlock();
    coordinator_set_desired_resource_mode(
        has_current ? resource_mode_for_owner(current.owner)
                    : PROTOCOL_RESOURCE_MODE_HOME);
    coordinator_set_discovery_suspended_for_policy(
        PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP, has_current);
}

static void coordinator_set_stop_required(ProtocolService service, bool required)
{
    coordinator_lock();
    g_coordinator.stop_required[service] = required;
    coordinator_unlock();
}

static void coordinator_service_worker_run(ProtocolServiceWorker *worker)
{
    ProtocolServiceOperations service_operations;
    void *context;
    bool started;

    coordinator_lock();
    service_operations = g_coordinator.operations.services[worker->service];
    context = g_coordinator.operations.context;
    coordinator_unlock();

    started = service_operations.start(context);
    coordinator_set_stop_required(
        worker->service,
        started || service_operations.stop_after_start_attempt);
    (void)protocol_coordinator_set_service_state(
        worker->service,
        started ? PROTOCOL_SERVICE_RUNNING : PROTOCOL_SERVICE_FAILED);
    coordinator_lock();
    worker->start_result = started;
    worker->start_finished = true;
    coordinator_unlock();
}

#ifdef __SWITCH__
static void coordinator_service_worker_main(void *opaque)
{
    coordinator_service_worker_run(opaque);
}
#else
static void *coordinator_service_worker_main(void *opaque)
{
    coordinator_service_worker_run(opaque);
    return NULL;
}
#endif

static bool coordinator_resource_transition_allowed(void)
{
    bool allowed;

    coordinator_lock();
    allowed = g_coordinator.runtime_active &&
              g_coordinator.lifecycle != PROTOCOL_LIFECYCLE_STOPPING &&
              g_coordinator.lifecycle != PROTOCOL_LIFECYCLE_STOPPED;
    coordinator_unlock();
    return allowed;
}

static bool coordinator_resource_target_current(ProtocolResourceMode target)
{
    bool current;

    coordinator_lock();
    current = g_coordinator.snapshot.desired_resource_mode == target;
    coordinator_unlock();
    return current;
}

static void coordinator_set_resource_start_call(ProtocolService service,
                                                bool active)
{
    coordinator_lock();
    g_coordinator.resource_worker.current_service =
        active ? service : PROTOCOL_SERVICE_COUNT;
    g_coordinator.resource_worker.start_call_active = active;
    coordinator_unlock();
}

static ProtocolServiceState coordinator_service_state(ProtocolService service)
{
    ProtocolServiceState state;

    coordinator_lock();
    state = g_coordinator.snapshot.services[service];
    coordinator_unlock();
    return state;
}

static void coordinator_clear_airplay_runtime_status(void)
{
    coordinator_lock();
    if (g_coordinator.snapshot.airplay.running ||
        g_coordinator.snapshot.airplay.starting ||
        g_coordinator.snapshot.airplay.pin_visible)
    {
        memset(&g_coordinator.snapshot.airplay, 0,
               sizeof(g_coordinator.snapshot.airplay));
        coordinator_bump_revision();
    }
    coordinator_unlock();
}

static void coordinator_resource_stop_service(
    ProtocolService service, const ProtocolCoordinatorOperations *operations)
{
    bool stop_required;

    coordinator_lock();
    stop_required = g_coordinator.stop_required[service];
    coordinator_unlock();
    if (operations->services[service].request_stop)
        operations->services[service].request_stop(operations->context);
    if (stop_required)
        operations->services[service].stop(operations->context);
    coordinator_lock();
    g_coordinator.stop_required[service] = false;
    coordinator_unlock();
    (void)protocol_coordinator_set_service_state(
        service, PROTOCOL_SERVICE_STOPPED);
    if (service == PROTOCOL_SERVICE_AIRPLAY)
        coordinator_clear_airplay_runtime_status();
}

static bool coordinator_resource_start_service(
    ProtocolService service, const ProtocolCoordinatorOperations *operations)
{
    bool started;
    bool cleanup_required;

    coordinator_lock();
    cleanup_required = g_coordinator.stop_required[service];
    coordinator_unlock();
    if (cleanup_required)
        coordinator_resource_stop_service(service, operations);
    if (!coordinator_resource_transition_allowed())
        return false;

    (void)protocol_coordinator_set_service_state(
        service, PROTOCOL_SERVICE_STARTING);
    started = operations->services[service].start(operations->context);
    coordinator_lock();
    g_coordinator.stop_required[service] =
        started || operations->services[service].stop_after_start_attempt;
    coordinator_unlock();
    (void)protocol_coordinator_set_service_state(
        service, started ? PROTOCOL_SERVICE_RUNNING
                         : PROTOCOL_SERVICE_FAILED);
    return started;
}

static void coordinator_resource_worker_run(ProtocolResourceWorker *worker)
{
    ProtocolCoordinatorOperations operations;
    bool enabled[PROTOCOL_SERVICE_COUNT];
    ProtocolResourceMode target;
    bool success = true;
    bool completed = false;
    int index;

    coordinator_lock();
    operations = g_coordinator.operations;
    memcpy(enabled, g_coordinator.configured_enabled, sizeof(enabled));
    target = worker->target;
    coordinator_unlock();

    if (target != PROTOCOL_RESOURCE_MODE_HOME &&
        !coordinator_set_background_network_suspended(true))
        success = false;

    for (index = PROTOCOL_SERVICE_COUNT - 1; index >= 0; --index)
    {
        ProtocolService service = (ProtocolService)index;
        ProtocolServiceState state = coordinator_service_state(service);

        if (!coordinator_resource_transition_allowed() ||
            !coordinator_resource_target_current(target))
            goto finished;
        if (state == PROTOCOL_SERVICE_DISABLED ||
            state == PROTOCOL_SERVICE_STOPPED ||
            coordinator_resource_service_required(target, service, enabled))
            continue;
        coordinator_resource_stop_service(service, &operations);
    }

    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        ProtocolService service = (ProtocolService)index;
        ProtocolServiceState state = coordinator_service_state(service);

        if (!coordinator_resource_transition_allowed() ||
            !coordinator_resource_target_current(target))
            goto finished;
        if (!coordinator_resource_service_required(target, service, enabled) ||
            state == PROTOCOL_SERVICE_RUNNING)
            continue;
        coordinator_set_resource_start_call(service, true);
        bool started = coordinator_resource_start_service(service, &operations);
        coordinator_set_resource_start_call(service, false);
        if (!started)
            success = false;
    }

    if (!coordinator_resource_target_current(target))
        goto finished;
    if (target == PROTOCOL_RESOURCE_MODE_HOME &&
        !coordinator_set_background_network_suspended(false))
        success = false;
    completed = true;

finished:
    coordinator_lock();
    worker->result = completed && success;
    worker->finished = true;
    g_coordinator.snapshot.resource_transition_active = false;
    if (completed && success)
    {
        g_coordinator.snapshot.applied_resource_mode = target;
        g_coordinator.snapshot.resource_transition_failed = false;
        g_coordinator.resource_retry_due_ms = 0u;
    }
    else if (completed)
    {
        g_coordinator.snapshot.resource_transition_failed = true;
        g_coordinator.resource_retry_due_ms =
            coordinator_monotonic_ms() + PROTOCOL_RESOURCE_RETRY_MS;
    }
    coordinator_bump_revision();
    coordinator_unlock();
}

#ifdef __SWITCH__
static void coordinator_resource_worker_main(void *opaque)
{
    coordinator_resource_worker_run(opaque);
}
#else
static void *coordinator_resource_worker_main(void *opaque)
{
    coordinator_resource_worker_run(opaque);
    return NULL;
}
#endif

static bool coordinator_launch_service_worker(ProtocolService service)
{
    ProtocolServiceWorker *worker = &g_coordinator.workers[service];

    memset(worker, 0, sizeof(*worker));
    worker->service = service;
#ifdef __SWITCH__
    int priority = service == PROTOCOL_SERVICE_AIRPLAY
                       ? PROTOCOL_SERVICE_AIRPLAY_WORKER_PRIORITY
                       : PROTOCOL_SERVICE_WORKER_PRIORITY;

    if (R_FAILED(threadCreate(&worker->thread,
                              coordinator_service_worker_main,
                              worker,
                              NULL,
                              PROTOCOL_SERVICE_WORKER_STACK_SIZE,
                              priority,
                              -2)))
        return false;
    if (R_FAILED(threadStart(&worker->thread)))
    {
        (void)threadClose(&worker->thread);
        memset(&worker->thread, 0, sizeof(worker->thread));
        return false;
    }
#else
    if (pthread_create(&worker->thread, NULL,
                       coordinator_service_worker_main, worker) != 0)
        return false;
#endif
    coordinator_lock();
    worker->thread_started = true;
    coordinator_unlock();
    return true;
}

static void coordinator_join_service_worker(ProtocolService service)
{
    ProtocolServiceWorker *worker = &g_coordinator.workers[service];
    bool thread_started;

    coordinator_lock();
    thread_started = worker->thread_started;
    coordinator_unlock();
    if (!thread_started)
        return;
#ifdef __SWITCH__
    (void)threadWaitForExit(&worker->thread);
    (void)threadClose(&worker->thread);
#else
    (void)pthread_join(worker->thread, NULL);
#endif
    coordinator_lock();
    worker->thread_started = false;
    coordinator_unlock();
}

static void coordinator_join_resource_worker(void)
{
    ProtocolResourceWorker *worker = &g_coordinator.resource_worker;
    bool thread_started;

    coordinator_lock();
    thread_started = worker->thread_started;
    coordinator_unlock();
    if (!thread_started)
        return;
#ifdef __SWITCH__
    (void)threadWaitForExit(&worker->thread);
    (void)threadClose(&worker->thread);
#else
    (void)pthread_join(worker->thread, NULL);
#endif
    coordinator_lock();
    worker->thread_started = false;
    coordinator_unlock();
}

static void coordinator_reap_finished_workers(void)
{
    bool finished[PROTOCOL_SERVICE_COUNT] = {false};
    int index;

    coordinator_lock();
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        finished[index] = g_coordinator.workers[index].thread_started &&
                          g_coordinator.workers[index].start_finished;
    }
    coordinator_unlock();

    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (finished[index])
            coordinator_join_service_worker((ProtocolService)index);
    }

    coordinator_lock();
    bool resource_finished = g_coordinator.resource_worker.thread_started &&
                             g_coordinator.resource_worker.finished;
    coordinator_unlock();
    if (resource_finished)
        coordinator_join_resource_worker();
}

static bool coordinator_service_worker_active(void)
{
    bool active = false;
    int index;

    coordinator_lock();
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (g_coordinator.workers[index].thread_started)
        {
            active = true;
            break;
        }
    }
    coordinator_unlock();
    return active;
}

static bool coordinator_launch_resource_worker(void)
{
    ProtocolResourceWorker *worker = &g_coordinator.resource_worker;
    ProtocolResourceMode target;
    uint64_t now_ms = coordinator_monotonic_ms();

    coordinator_lock();
    if (!g_coordinator.runtime_active ||
        !g_coordinator.exclusive_media_resources ||
        worker->thread_started ||
        g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPING ||
        g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPED ||
        (g_coordinator.snapshot.desired_resource_mode ==
             g_coordinator.snapshot.applied_resource_mode &&
         coordinator_resource_services_match_locked(
             g_coordinator.snapshot.desired_resource_mode)) ||
        (g_coordinator.snapshot.resource_transition_failed &&
         now_ms < g_coordinator.resource_retry_due_ms))
    {
        coordinator_unlock();
        return false;
    }
    target = g_coordinator.snapshot.desired_resource_mode;
    coordinator_unlock();
    if (coordinator_service_worker_active())
        return false;

    memset(worker, 0, sizeof(*worker));
    worker->target = target;
    worker->current_service = PROTOCOL_SERVICE_COUNT;
    coordinator_lock();
    g_coordinator.snapshot.resource_transition_active = true;
    g_coordinator.snapshot.resource_transition_failed = false;
    g_coordinator.resource_transition_ms = now_ms;
    coordinator_bump_revision();
    coordinator_unlock();
#ifdef __SWITCH__
    if (R_FAILED(threadCreate(&worker->thread,
                              coordinator_resource_worker_main,
                              worker,
                              NULL,
                              PROTOCOL_RESOURCE_WORKER_STACK_SIZE,
                              PROTOCOL_RESOURCE_WORKER_PRIORITY,
                              -2)))
        goto failure;
    if (R_FAILED(threadStart(&worker->thread)))
    {
        (void)threadClose(&worker->thread);
        memset(&worker->thread, 0, sizeof(worker->thread));
        goto failure;
    }
#else
    if (pthread_create(&worker->thread, NULL,
                       coordinator_resource_worker_main, worker) != 0)
        goto failure;
#endif
    coordinator_lock();
    worker->thread_started = true;
    coordinator_unlock();
    return true;

failure:
    coordinator_lock();
    g_coordinator.snapshot.resource_transition_active = false;
    g_coordinator.snapshot.resource_transition_failed = true;
    g_coordinator.resource_retry_due_ms = now_ms + PROTOCOL_RESOURCE_RETRY_MS;
    coordinator_bump_revision();
    coordinator_unlock();
    return false;
}

static bool coordinator_launch_next_serial_worker(void)
{
    ProtocolService service = PROTOCOL_SERVICE_COUNT;
    bool worker_active = false;
    int index;

    coordinator_lock();
    if (!g_coordinator.runtime_active || !g_coordinator.serial_startup ||
        g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPING ||
        g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPED)
    {
        coordinator_unlock();
        return false;
    }
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (g_coordinator.workers[index].thread_started)
        {
            worker_active = true;
            break;
        }
    }
    if (!worker_active)
    {
        while (g_coordinator.next_service_to_start < PROTOCOL_SERVICE_COUNT)
        {
            index = g_coordinator.next_service_to_start++;
            if (g_coordinator.snapshot.services[index] !=
                PROTOCOL_SERVICE_DISABLED)
            {
                service = (ProtocolService)index;
                break;
            }
        }
    }
    coordinator_unlock();

    if (service == PROTOCOL_SERVICE_COUNT)
        return false;
    if (!coordinator_launch_service_worker(service))
    {
        (void)protocol_coordinator_set_service_state(
            service, PROTOCOL_SERVICE_FAILED);
    }
    return true;
}

static bool airplay_status_equal(const ProtocolAirPlayStatus *left,
                                 const ProtocolAirPlayStatus *right)
{
    return left->running == right->running &&
           left->starting == right->starting &&
           left->pin_visible == right->pin_visible &&
           memcmp(left->pin, right->pin, sizeof(left->pin)) == 0 &&
           strcmp(left->status, right->status) == 0;
}

void protocol_coordinator_reset(void)
{
    PlayerOwnershipLease empty = {0};
    uint32_t revision;
    bool runtime_active;

    coordinator_lock();
    runtime_active = g_coordinator.runtime_active;
    coordinator_unlock();
    if (runtime_active)
        protocol_coordinator_stop();

    player_ownership_transition_begin();
    player_ownership_reset();
    coordinator_lock();
    revision = g_coordinator.snapshot.revision;
    memset(&g_coordinator.snapshot, 0, sizeof(g_coordinator.snapshot));
    memset(&g_coordinator.operations, 0, sizeof(g_coordinator.operations));
    memset(g_coordinator.stop_required, 0,
           sizeof(g_coordinator.stop_required));
    memset(g_coordinator.workers, 0, sizeof(g_coordinator.workers));
    memset(&g_coordinator.resource_worker, 0,
           sizeof(g_coordinator.resource_worker));
    memset(g_coordinator.service_transition_ms, 0,
           sizeof(g_coordinator.service_transition_ms));
    g_coordinator.runtime_active = false;
    g_coordinator.serial_startup = false;
    g_coordinator.exclusive_media_resources = false;
    memset(g_coordinator.configured_enabled, 0,
           sizeof(g_coordinator.configured_enabled));
    g_coordinator.background_network_suspended = false;
    g_coordinator.discovery_suspension_policy =
        PROTOCOL_DISCOVERY_SUSPEND_NONE;
    g_coordinator.next_service_to_start = 0;
    g_coordinator.resource_transition_ms = 0u;
    g_coordinator.resource_retry_due_ms = 0u;
    g_coordinator.media_claim_ms = 0u;
    g_coordinator.media_ready_ms = 0u;
    g_coordinator.playback_observed_generation = 0u;
    g_coordinator.playback_seen_active = false;
    g_coordinator.snapshot.active_media = empty;
    g_coordinator.snapshot.desired_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.snapshot.applied_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.lifecycle = PROTOCOL_LIFECYCLE_STOPPED;
    g_coordinator.snapshot.state = PROTOCOL_COORDINATOR_STOPPED;
    g_coordinator.snapshot.revision = revision;
    coordinator_bump_revision();
    coordinator_unlock();
    player_ownership_transition_end();
}

bool protocol_coordinator_start(const ProtocolCoordinatorConfig *config,
                                const ProtocolCoordinatorOperations *operations)
{
    int index;

    if (!operations_valid(config, operations) ||
        !protocol_coordinator_begin_start(config))
        return false;

    coordinator_lock();
    g_coordinator.operations = *operations;
    g_coordinator.runtime_active = true;
    g_coordinator.serial_startup = config->serial_startup;
    g_coordinator.exclusive_media_resources =
        config->exclusive_media_resources;
    memcpy(g_coordinator.configured_enabled, config->enabled,
           sizeof(g_coordinator.configured_enabled));
    g_coordinator.next_service_to_start = 0;
    coordinator_unlock();

    if (config->serial_startup)
    {
        (void)coordinator_launch_next_serial_worker();
        return true;
    }

    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (!config->enabled[index])
            continue;
        if (!coordinator_launch_service_worker((ProtocolService)index))
        {
            (void)protocol_coordinator_set_service_state(
                (ProtocolService)index, PROTOCOL_SERVICE_FAILED);
        }
    }

    return true;
}

void protocol_coordinator_tick(void)
{
    ProtocolCoordinatorOperations operations;
    ProtocolAirPlayStatus airplay = {0};
    bool runtime_active;
    bool poll_airplay;

    coordinator_reap_finished_workers();
    (void)coordinator_launch_next_serial_worker();
    coordinator_sync_active_media();
    (void)coordinator_launch_resource_worker();
    coordinator_lock();
    runtime_active = g_coordinator.runtime_active;
    operations = g_coordinator.operations;
    poll_airplay = runtime_active &&
                   g_coordinator.stop_required[PROTOCOL_SERVICE_AIRPLAY] &&
                   g_coordinator.lifecycle != PROTOCOL_LIFECYCLE_STOPPING &&
                   g_coordinator.lifecycle != PROTOCOL_LIFECYCLE_STOPPED;
    coordinator_unlock();

    if (!poll_airplay ||
        !operations.airplay_get_status(operations.context, &airplay))
        return;

    airplay.pin[sizeof(airplay.pin) - 1u] = '\0';
    airplay.status[sizeof(airplay.status) - 1u] = '\0';
    coordinator_lock();
    if (!airplay_status_equal(&g_coordinator.snapshot.airplay, &airplay))
    {
        g_coordinator.snapshot.airplay = airplay;
        coordinator_bump_revision();
    }
    coordinator_unlock();

    (void)protocol_coordinator_set_service_state(
        PROTOCOL_SERVICE_AIRPLAY,
        airplay.running ? PROTOCOL_SERVICE_RUNNING :
        (airplay.starting ? PROTOCOL_SERVICE_STARTING : PROTOCOL_SERVICE_FAILED));
}

void protocol_coordinator_stop(void)
{
    ProtocolCoordinatorOperations operations;
    bool stop_required[PROTOCOL_SERVICE_COUNT];
    bool request_stop[PROTOCOL_SERVICE_COUNT] = {false};
    bool runtime_active;
    int index;

    if (!protocol_coordinator_begin_stop())
        return;

    coordinator_lock();
    runtime_active = g_coordinator.runtime_active;
    operations = g_coordinator.operations;
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        request_stop[index] = runtime_active &&
                              g_coordinator.snapshot.services[index] !=
                                  PROTOCOL_SERVICE_DISABLED &&
                              operations.services[index].request_stop != NULL;
    }
    coordinator_unlock();
    for (index = PROTOCOL_SERVICE_COUNT - 1; index >= 0; --index)
    {
        if (request_stop[index])
            operations.services[index].request_stop(operations.context);
    }

    coordinator_join_resource_worker();
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
        coordinator_join_service_worker((ProtocolService)index);

    coordinator_lock();
    runtime_active = g_coordinator.runtime_active;
    operations = g_coordinator.operations;
    memcpy(stop_required, g_coordinator.stop_required, sizeof(stop_required));
    coordinator_unlock();

    for (index = PROTOCOL_SERVICE_COUNT - 1; index >= 0; --index)
    {
        if (!runtime_active || !stop_required[index])
            continue;
        operations.services[index].stop(operations.context);
        (void)protocol_coordinator_set_service_state(
            (ProtocolService)index, PROTOCOL_SERVICE_STOPPED);
    }

    protocol_coordinator_finish_stop();
    (void)coordinator_set_background_network_suspended(false);
    coordinator_lock();
    memset(&g_coordinator.operations, 0, sizeof(g_coordinator.operations));
    memset(g_coordinator.stop_required, 0,
           sizeof(g_coordinator.stop_required));
    g_coordinator.runtime_active = false;
    g_coordinator.serial_startup = false;
    g_coordinator.exclusive_media_resources = false;
    memset(g_coordinator.configured_enabled, 0,
           sizeof(g_coordinator.configured_enabled));
    memset(&g_coordinator.resource_worker, 0,
           sizeof(g_coordinator.resource_worker));
    g_coordinator.discovery_suspension_policy =
        PROTOCOL_DISCOVERY_SUSPEND_NONE;
    g_coordinator.next_service_to_start = 0;
    coordinator_unlock();
}

bool protocol_coordinator_begin_start(const ProtocolCoordinatorConfig *config)
{
    int index;

    if (!config)
        return false;

    coordinator_lock();
    if (g_coordinator.lifecycle != PROTOCOL_LIFECYCLE_STOPPED)
    {
        coordinator_unlock();
        return false;
    }

    memset(&g_coordinator.snapshot.active_media, 0,
           sizeof(g_coordinator.snapshot.active_media));
    g_coordinator.snapshot.desired_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.snapshot.applied_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.snapshot.resource_transition_active = false;
    g_coordinator.snapshot.resource_transition_failed = false;
    g_coordinator.media_claim_ms = 0u;
    g_coordinator.media_ready_ms = 0u;
    g_coordinator.playback_observed_generation = 0u;
    g_coordinator.playback_seen_active = false;
    g_coordinator.exclusive_media_resources =
        config->exclusive_media_resources;
    memcpy(g_coordinator.configured_enabled, config->enabled,
           sizeof(g_coordinator.configured_enabled));
    g_coordinator.snapshot.discovery_suspended = false;
    g_coordinator.discovery_suspension_policy =
        config->discovery_suspension_policy;
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        g_coordinator.snapshot.services[index] =
            config->enabled[index] ? PROTOCOL_SERVICE_STARTING
                                   : PROTOCOL_SERVICE_DISABLED;
        g_coordinator.service_transition_ms[index] =
            coordinator_monotonic_ms();
    }
    g_coordinator.lifecycle = PROTOCOL_LIFECYCLE_STARTING;
    coordinator_recompute_state();
    coordinator_bump_revision();
    coordinator_unlock();
    return true;
}

bool protocol_coordinator_set_service_state(ProtocolService service,
                                            ProtocolServiceState state)
{
    if (!service_valid(service) || !service_state_valid(state))
        return false;

    coordinator_lock();
    if (g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPED)
    {
        coordinator_unlock();
        return false;
    }
    if (g_coordinator.snapshot.services[service] != state)
    {
        g_coordinator.snapshot.services[service] = state;
        g_coordinator.service_transition_ms[service] =
            coordinator_monotonic_ms();
        coordinator_recompute_state();
        coordinator_bump_revision();
    }
    coordinator_unlock();
    return true;
}

bool protocol_coordinator_begin_stop(void)
{
    coordinator_lock();
    if (g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPED ||
        g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STOPPING)
    {
        coordinator_unlock();
        return false;
    }
    g_coordinator.lifecycle = PROTOCOL_LIFECYCLE_STOPPING;
    coordinator_recompute_state();
    coordinator_bump_revision();
    coordinator_unlock();
    return true;
}

void protocol_coordinator_finish_stop(void)
{
    int index;

    player_ownership_transition_begin();
    player_ownership_reset();
    coordinator_set_discovery_suspended(false);
    coordinator_lock();
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        if (g_coordinator.snapshot.services[index] != PROTOCOL_SERVICE_DISABLED)
        {
            g_coordinator.snapshot.services[index] = PROTOCOL_SERVICE_STOPPED;
            g_coordinator.service_transition_ms[index] =
                coordinator_monotonic_ms();
        }
    }
    memset(&g_coordinator.snapshot.active_media, 0,
           sizeof(g_coordinator.snapshot.active_media));
    g_coordinator.media_claim_ms = 0u;
    g_coordinator.media_ready_ms = 0u;
    g_coordinator.playback_observed_generation = 0u;
    g_coordinator.playback_seen_active = false;
    g_coordinator.snapshot.desired_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.snapshot.applied_resource_mode =
        PROTOCOL_RESOURCE_MODE_HOME;
    g_coordinator.snapshot.resource_transition_active = false;
    g_coordinator.snapshot.resource_transition_failed = false;
    g_coordinator.discovery_suspension_policy =
        PROTOCOL_DISCOVERY_SUSPEND_NONE;
    g_coordinator.lifecycle = PROTOCOL_LIFECYCLE_STOPPED;
    coordinator_recompute_state();
    coordinator_bump_revision();
    coordinator_unlock();
    player_ownership_transition_end();
}

void protocol_coordinator_set_playback_active(bool active)
{
    coordinator_set_discovery_suspended_for_policy(
        PROTOCOL_DISCOVERY_SUSPEND_PLAYBACK_ACTIVITY, active);
}

void protocol_coordinator_observe_playback(bool active, bool terminal)
{
    PlayerOwnershipLease lease = {0};
    uint64_t now_ms = coordinator_monotonic_ms();
    bool seen_active = false;
    bool release = false;

    protocol_coordinator_set_playback_active(active);
    coordinator_lock();
    if (g_coordinator.exclusive_media_resources &&
        g_coordinator.snapshot.active_media.owner != PLAYER_MEDIA_OWNER_NONE)
    {
        if (g_coordinator.playback_observed_generation !=
            g_coordinator.snapshot.active_media.generation)
        {
            g_coordinator.playback_observed_generation =
                g_coordinator.snapshot.active_media.generation;
            g_coordinator.playback_seen_active = false;
            g_coordinator.media_claim_ms = now_ms;
        }
        if (active)
        {
            g_coordinator.playback_seen_active = true;
            g_coordinator.media_ready_ms = 0u;
        }
        seen_active = g_coordinator.playback_seen_active;
        if (!active && terminal && !seen_active)
        {
            bool resource_ready =
                g_coordinator.snapshot.desired_resource_mode ==
                g_coordinator.snapshot.applied_resource_mode &&
                !g_coordinator.snapshot.resource_transition_active &&
                !g_coordinator.snapshot.resource_transition_failed &&
                coordinator_resource_services_match_locked(
                    g_coordinator.snapshot.desired_resource_mode);

            if (!resource_ready)
                g_coordinator.media_ready_ms = 0u;
            else if (g_coordinator.media_ready_ms == 0u)
                g_coordinator.media_ready_ms = now_ms;
        }
        if (terminal &&
            (g_coordinator.playback_seen_active ||
             (g_coordinator.media_ready_ms > 0u &&
              now_ms >= g_coordinator.media_ready_ms &&
              now_ms - g_coordinator.media_ready_ms >=
                  PROTOCOL_TERMINAL_RELEASE_GRACE_MS)))
        {
            lease = g_coordinator.snapshot.active_media;
            release = true;
        }
    }
    coordinator_unlock();

    if (release)
    {
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=terminal-release owner=%s "
            "token=%llu generation=%u seen_active=%d\n",
            player_media_owner_name(lease.owner),
            (unsigned long long)lease.token, lease.generation,
            seen_active ? 1 : 0);
        (void)protocol_coordinator_media_release(&lease);
    }
}

bool protocol_coordinator_get_snapshot(ProtocolCoordinatorSnapshot *snapshot_out)
{
    uint64_t now_ms;
    int index;

    if (!snapshot_out)
        return false;

    coordinator_lock();
    *snapshot_out = g_coordinator.snapshot;
    now_ms = coordinator_monotonic_ms();
    for (index = 0; index < PROTOCOL_SERVICE_COUNT; ++index)
    {
        snapshot_out->service_worker_active[index] =
            g_coordinator.workers[index].thread_started &&
            !g_coordinator.workers[index].start_finished;
        snapshot_out->service_transition_age_ms[index] =
            g_coordinator.service_transition_ms[index] > 0 &&
                    now_ms >= g_coordinator.service_transition_ms[index]
                ? now_ms - g_coordinator.service_transition_ms[index]
                : 0;
    }
    snapshot_out->resource_transition_age_ms =
        g_coordinator.snapshot.resource_transition_active &&
                g_coordinator.resource_transition_ms > 0u &&
                now_ms >= g_coordinator.resource_transition_ms
            ? now_ms - g_coordinator.resource_transition_ms
            : 0u;
    coordinator_unlock();
    return true;
}

bool protocol_coordinator_media_begin(PlayerMediaOwner owner, uint64_t token,
                                      ProtocolMediaTransaction *transaction_out)
{
    PlayerOwnershipLease lease;
    PlayerOwnershipLease previous = {0};
    bool has_previous;

    if (!transaction_out || owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    memset(transaction_out, 0, sizeof(*transaction_out));
    if (!coordinator_accepts_media())
    {
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=claim owner=%s "
            "token=%llu reason=lifecycle\n",
            player_media_owner_name(owner), (unsigned long long)token);
        return false;
    }

    player_ownership_transition_begin();
    has_previous = player_ownership_current(&previous);
    if (!coordinator_accepts_media() ||
        (coordinator_exclusive_resources_enabled() && has_previous &&
         resource_mode_for_owner(previous.owner) !=
             resource_mode_for_owner(owner)))
    {
        player_ownership_transition_end();
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=claim owner=%s "
            "token=%llu current_owner=%s current_token=%llu "
            "reason=exclusive-owner\n",
            player_media_owner_name(owner), (unsigned long long)token,
            player_media_owner_name(previous.owner),
            (unsigned long long)previous.token);
        return false;
    }
    if (!player_ownership_claim(owner, token, &lease, &previous))
    {
        player_ownership_transition_end();
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=claim owner=%s "
            "token=%llu reason=lifecycle-changed\n",
            player_media_owner_name(owner), (unsigned long long)token);
        return false;
    }

    transaction_out->active = true;
    transaction_out->lease = lease;
    transaction_out->previous = previous;
    coordinator_set_active_media(&lease);
    coordinator_set_desired_resource_mode(resource_mode_for_owner(owner));
    coordinator_set_discovery_suspended_for_policy(
        PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP, true);
    player_ownership_transition_end();
    COORDINATOR_LOG_INFO(
        "[protocol-coordinator] event=claim previous_owner=%s "
        "previous_token=%llu previous_generation=%u owner=%s token=%llu "
        "generation=%u\n",
        player_media_owner_name(previous.owner),
        (unsigned long long)previous.token, previous.generation,
        player_media_owner_name(lease.owner), (unsigned long long)lease.token,
        lease.generation);
    runtime_observability_log_resources(
        "claim", player_media_owner_name(lease.owner), lease.generation);
    return true;
}

bool protocol_coordinator_media_validate(const PlayerOwnershipLease *lease)
{
    PlayerOwnershipLease current = {0};
    bool valid = player_ownership_validate(lease);

    if (!valid)
    {
        (void)player_ownership_current(&current);
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=validate "
            "owner=%s token=%llu generation=%u current_owner=%s "
            "current_token=%llu current_generation=%u reason=stale-lease\n",
            player_media_owner_name(lease ? lease->owner : PLAYER_MEDIA_OWNER_NONE),
            (unsigned long long)(lease ? lease->token : 0u),
            lease ? lease->generation : 0u,
            player_media_owner_name(current.owner),
            (unsigned long long)current.token, current.generation);
    }
    return valid;
}

bool protocol_coordinator_media_wait_resources(
    const PlayerOwnershipLease *lease, uint32_t timeout_ms)
{
    uint64_t start_ms;

    if (!lease || lease->owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    start_ms = coordinator_monotonic_ms();
    for (;;)
    {
        ProtocolResourceMode expected = resource_mode_for_owner(lease->owner);
        bool exclusive;
        bool ready;
        bool failed;
        bool accepting;

        coordinator_lock();
        exclusive = g_coordinator.exclusive_media_resources;
        accepting = g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_STARTING ||
                    g_coordinator.lifecycle == PROTOCOL_LIFECYCLE_RUNNING;
        ready = g_coordinator.snapshot.active_media.owner == lease->owner &&
                g_coordinator.snapshot.active_media.token == lease->token &&
                g_coordinator.snapshot.active_media.generation ==
                    lease->generation &&
                (!exclusive ||
                 (g_coordinator.snapshot.applied_resource_mode == expected &&
                  !g_coordinator.snapshot.resource_transition_active &&
                  coordinator_resource_services_match_locked(expected)));
        failed = g_coordinator.snapshot.resource_transition_failed;
        coordinator_unlock();
        if (ready)
            return protocol_coordinator_media_validate(lease);
        if (!accepting || failed ||
            !protocol_coordinator_media_validate(lease))
            return false;
        if (timeout_ms != UINT32_MAX &&
            coordinator_monotonic_ms() - start_ms >= timeout_ms)
            return false;
        coordinator_sleep_ms(1u);
    }
}

bool protocol_coordinator_media_drive_resources(
    const PlayerOwnershipLease *lease, uint32_t timeout_ms)
{
    uint64_t start_ms;

    if (!lease || lease->owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    start_ms = coordinator_monotonic_ms();
    for (;;)
    {
        protocol_coordinator_tick();
        if (protocol_coordinator_media_wait_resources(lease, 0u))
            return true;
        if (!protocol_coordinator_media_validate(lease))
            return false;
        if (timeout_ms != UINT32_MAX &&
            coordinator_monotonic_ms() - start_ms >= timeout_ms)
            return false;
        coordinator_sleep_ms(1u);
    }
}

bool protocol_coordinator_media_release(const PlayerOwnershipLease *lease)
{
    bool released;

    player_ownership_transition_begin();
    released = player_ownership_release(lease);
    if (released)
    {
        coordinator_set_active_media(NULL);
        coordinator_set_desired_resource_mode(PROTOCOL_RESOURCE_MODE_HOME);
        coordinator_set_discovery_suspended_for_policy(
            PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP, false);
    }
    player_ownership_transition_end();
    COORDINATOR_LOG_INFO(
        "[protocol-coordinator] event=%s action=release owner=%s token=%llu "
        "generation=%u reason=%s\n",
        released ? "release" : "reject",
        player_media_owner_name(lease ? lease->owner : PLAYER_MEDIA_OWNER_NONE),
        (unsigned long long)(lease ? lease->token : 0u),
        lease ? lease->generation : 0u,
        released ? "completed" : "stale-lease");
    return released;
}

void protocol_coordinator_media_abort(ProtocolMediaTransaction *transaction)
{
    bool released;

    if (!transaction || !transaction->active)
        return;
    player_ownership_transition_begin();
    released = player_ownership_release(&transaction->lease);
    if (released)
    {
        coordinator_set_active_media(NULL);
        coordinator_set_desired_resource_mode(PROTOCOL_RESOURCE_MODE_HOME);
        coordinator_set_discovery_suspended_for_policy(
            PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP, false);
    }
    player_ownership_transition_end();
    COORDINATOR_LOG_INFO(
        "[protocol-coordinator] event=abort owner=%s token=%llu "
        "generation=%u released=%d\n",
        player_media_owner_name(transaction->lease.owner),
        (unsigned long long)transaction->lease.token,
        transaction->lease.generation, released ? 1 : 0);
    protocol_coordinator_media_end(transaction);
}

void protocol_coordinator_media_end(ProtocolMediaTransaction *transaction)
{
    if (!transaction || !transaction->active)
        return;
    transaction->active = false;
}

bool protocol_coordinator_media_guard_begin(PlayerMediaOwner owner, uint64_t token,
                                            ProtocolMediaGuard *guard_out)
{
    PlayerOwnershipLease lease = {0};
    bool has_owner;

    if (!guard_out || owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    memset(guard_out, 0, sizeof(*guard_out));
    if (!coordinator_accepts_media())
    {
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=guard owner=%s "
            "token=%llu reason=lifecycle\n",
            player_media_owner_name(owner), (unsigned long long)token);
        return false;
    }

    player_ownership_transition_begin();
    has_owner = player_ownership_current(&lease);
    if (!coordinator_accepts_media() || !has_owner ||
        lease.owner != owner || lease.token != token)
    {
        player_ownership_transition_end();
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=guard owner=%s "
            "token=%llu current_owner=%s current_token=%llu "
            "current_generation=%u reason=owner-mismatch\n",
            player_media_owner_name(owner), (unsigned long long)token,
            player_media_owner_name(lease.owner),
            (unsigned long long)lease.token, lease.generation);
        return false;
    }
    guard_out->active = true;
    guard_out->lease = lease;
    player_ownership_transition_end();
    return true;
}

bool protocol_coordinator_media_unowned_or_owner_guard_begin(
    PlayerMediaOwner owner, uint64_t token, ProtocolMediaGuard *guard_out)
{
    PlayerOwnershipLease lease = {0};
    bool has_owner;

    if (!guard_out || owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    memset(guard_out, 0, sizeof(*guard_out));
    if (!coordinator_accepts_guard(false))
    {
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=unowned-guard "
            "owner=%s token=%llu reason=lifecycle\n",
            player_media_owner_name(owner), (unsigned long long)token);
        return false;
    }

    player_ownership_transition_begin();
    has_owner = player_ownership_current(&lease);
    if (!coordinator_accepts_guard(false) ||
        (has_owner && (lease.owner != owner || lease.token != token)))
    {
        player_ownership_transition_end();
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=unowned-guard "
            "owner=%s token=%llu current_owner=%s current_token=%llu "
            "current_generation=%u reason=owner-mismatch\n",
            player_media_owner_name(owner), (unsigned long long)token,
            player_media_owner_name(lease.owner),
            (unsigned long long)lease.token, lease.generation);
        return false;
    }
    guard_out->active = true;
    guard_out->lease = lease;
    player_ownership_transition_end();
    return true;
}

bool protocol_coordinator_media_lease_guard_begin(
    const PlayerOwnershipLease *lease, bool allow_stopping,
    ProtocolMediaGuard *guard_out)
{
    if (!lease || !guard_out || lease->owner == PLAYER_MEDIA_OWNER_NONE)
        return false;
    memset(guard_out, 0, sizeof(*guard_out));
    if (!coordinator_accepts_guard(allow_stopping))
    {
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=lease-guard "
            "owner=%s token=%llu generation=%u allow_stopping=%d "
            "reason=lifecycle\n",
            player_media_owner_name(lease->owner),
            (unsigned long long)lease->token, lease->generation,
            allow_stopping ? 1 : 0);
        return false;
    }

    player_ownership_transition_begin();
    if (!coordinator_accepts_guard(allow_stopping) ||
        !player_ownership_validate(lease))
    {
        player_ownership_transition_end();
        COORDINATOR_LOG_INFO(
            "[protocol-coordinator] event=reject action=lease-guard "
            "owner=%s token=%llu generation=%u allow_stopping=%d "
            "reason=stale-lease\n",
            player_media_owner_name(lease->owner),
            (unsigned long long)lease->token, lease->generation,
            allow_stopping ? 1 : 0);
        return false;
    }
    guard_out->active = true;
    guard_out->lease = *lease;
    player_ownership_transition_end();
    return true;
}

bool protocol_coordinator_media_barrier_begin(ProtocolMediaGuard *guard_out)
{
    if (!guard_out)
        return false;
    memset(guard_out, 0, sizeof(*guard_out));
    player_ownership_transition_begin();
    guard_out->active = true;
    (void)player_ownership_current(&guard_out->lease);
    player_ownership_transition_end();
    return true;
}

void protocol_coordinator_media_guard_end(ProtocolMediaGuard *guard)
{
    if (!guard || !guard->active)
        return;
    guard->active = false;
}

bool protocol_coordinator_media_current(PlayerOwnershipLease *lease_out)
{
    return player_ownership_current(lease_out);
}

bool protocol_coordinator_media_release_current(PlayerMediaOwner owner,
                                                uint64_t token)
{
    bool released;

    player_ownership_transition_begin();
    released = player_ownership_release_current(owner, token);
    if (released)
    {
        coordinator_set_active_media(NULL);
        coordinator_set_desired_resource_mode(PROTOCOL_RESOURCE_MODE_HOME);
        coordinator_set_discovery_suspended_for_policy(
            PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP, false);
    }
    player_ownership_transition_end();
    COORDINATOR_LOG_INFO(
        "[protocol-coordinator] event=%s action=release-current owner=%s "
        "token=%llu reason=%s\n",
        released ? "release" : "reject", player_media_owner_name(owner),
        (unsigned long long)token,
        released ? "completed" : "owner-mismatch");
    return released;
}

const char *protocol_coordinator_state_name(ProtocolCoordinatorState state)
{
    switch (state)
    {
    case PROTOCOL_COORDINATOR_STOPPED:
        return "stopped";
    case PROTOCOL_COORDINATOR_STARTING:
        return "starting";
    case PROTOCOL_COORDINATOR_READY:
        return "ready";
    case PROTOCOL_COORDINATOR_MEDIA_ACTIVE:
        return "media-active";
    case PROTOCOL_COORDINATOR_STOPPING:
        return "stopping";
    case PROTOCOL_COORDINATOR_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

const char *protocol_service_name(ProtocolService service)
{
    switch (service)
    {
    case PROTOCOL_SERVICE_IPTV:
        return "iptv";
    case PROTOCOL_SERVICE_DLNA:
        return "dlna";
    case PROTOCOL_SERVICE_AIRPLAY:
        return "airplay";
    default:
        return "unknown";
    }
}

const char *protocol_service_state_name(ProtocolServiceState state)
{
    switch (state)
    {
    case PROTOCOL_SERVICE_DISABLED:
        return "disabled";
    case PROTOCOL_SERVICE_STOPPED:
        return "stopped";
    case PROTOCOL_SERVICE_STARTING:
        return "starting";
    case PROTOCOL_SERVICE_RUNNING:
        return "running";
    case PROTOCOL_SERVICE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

const char *protocol_resource_mode_name(ProtocolResourceMode mode)
{
    switch (mode)
    {
    case PROTOCOL_RESOURCE_MODE_HOME:
        return "home";
    case PROTOCOL_RESOURCE_MODE_IPTV_EXCLUSIVE:
        return "iptv-exclusive";
    case PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE:
        return "dlna-exclusive";
    case PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE:
        return "airplay-exclusive";
    default:
        return "unknown";
    }
}
