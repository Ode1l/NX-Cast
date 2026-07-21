#define _POSIX_C_SOURCE 200809L

#include "media_actor.h"

#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
typedef Mutex ActorMutex;
typedef CondVar ActorCond;
typedef Thread ActorThread;
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t ActorMutex;
typedef pthread_cond_t ActorCond;
typedef pthread_t ActorThread;
#endif

typedef struct MediaActorRequest
{
    MediaActorCommand command;
    char *text;
    char *metadata;
    MediaActorSubmitStatus completion_status;
    bool completed;
    bool opaque_retained;
    unsigned int references;
    struct MediaActorRequest *next_free;
} MediaActorRequest;

struct MediaActor
{
    ActorMutex mutex;
    ActorCond cond;
    ActorThread thread;
    MediaActorConfig config;
    MediaActorRequest **queue;
    size_t queue_head;
    size_t queue_count;
    bool thread_started;
    bool join_in_progress;
    bool running;
    bool stopping;
    bool accepting;
    bool dispatch_requested;
    bool worker_identity_valid;
#ifdef __SWITCH__
    Handle worker_handle;
#else
    pthread_t worker_thread;
#endif
    MediaActorHealth health;
    uint64_t current_command_started_ms;
    uint64_t last_heartbeat_ms;
};

static void actor_lock(MediaActor *actor)
{
#ifdef __SWITCH__
    mutexLock(&actor->mutex);
#else
    (void)pthread_mutex_lock(&actor->mutex);
#endif
}

static void actor_unlock(MediaActor *actor)
{
#ifdef __SWITCH__
    mutexUnlock(&actor->mutex);
#else
    (void)pthread_mutex_unlock(&actor->mutex);
#endif
}

static void actor_wake_all_locked(MediaActor *actor)
{
#ifdef __SWITCH__
    (void)condvarWakeAll(&actor->cond);
#else
    (void)pthread_cond_broadcast(&actor->cond);
#endif
}

static void actor_mark_start_failed_locked(MediaActor *actor)
{
    actor->running = false;
    actor->accepting = false;
    actor->dispatch_requested = false;
    actor->health.running = false;
    actor->health.accepting = false;
    actor->health.initializing = false;
    actor->health.ready = false;
    actor->health.initialization_failed = true;
    actor->health.dispatch_enabled = false;
}

static uint64_t monotonic_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
#else
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
#endif
}

static void actor_wait_locked(MediaActor *actor, uint32_t timeout_ms)
{
#ifdef __SWITCH__
    if (timeout_ms == MEDIA_ACTOR_WAIT_FOREVER)
        (void)condvarWait(&actor->cond, &actor->mutex);
    else
        (void)condvarWaitTimeout(&actor->cond, &actor->mutex,
                                 (uint64_t)timeout_ms * 1000000ULL);
#else
    if (timeout_ms == MEDIA_ACTOR_WAIT_FOREVER)
    {
        (void)pthread_cond_wait(&actor->cond, &actor->mutex);
    }
    else
    {
        struct timespec deadline;
        uint64_t nanoseconds;

        (void)clock_gettime(CLOCK_REALTIME, &deadline);
        nanoseconds = (uint64_t)deadline.tv_nsec +
                      (uint64_t)timeout_ms * 1000000ULL;
        deadline.tv_sec += (time_t)(nanoseconds / 1000000000ULL);
        deadline.tv_nsec = (long)(nanoseconds % 1000000000ULL);
        (void)pthread_cond_timedwait(&actor->cond, &actor->mutex, &deadline);
    }
#endif
}

static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (!value)
        return NULL;
    length = strlen(value) + 1;
    copy = malloc(length);
    if (copy)
        memcpy(copy, value, length);
    return copy;
}

static void request_free(MediaActorRequest *request)
{
    if (!request)
        return;
    free(request->text);
    free(request->metadata);
    if (request->opaque_retained && request->command.opaque_release)
        request->command.opaque_release(request->command.opaque);
    free(request);
}

static void request_free_list(MediaActorRequest *request)
{
    while (request)
    {
        MediaActorRequest *next = request->next_free;
        request_free(request);
        request = next;
    }
}

static MediaActorRequest *request_create(const MediaActorCommand *command)
{
    MediaActorRequest *request;

    request = calloc(1, sizeof(*request));
    if (!request)
        return NULL;
    request->command = *command;
    request->command.command_id = 0;
    request->text = duplicate_string(command->text);
    request->metadata = duplicate_string(command->metadata);
    if ((command->text && !request->text) ||
        (command->metadata && !request->metadata))
    {
        request_free(request);
        return NULL;
    }
    request->command.text = request->text;
    request->command.metadata = request->metadata;
    if (request->command.opaque && request->command.opaque_retain)
    {
        request->command.opaque_retain(request->command.opaque);
        request->opaque_retained = true;
    }
    return request;
}

static bool command_valid(const MediaActorCommand *command)
{
    return command && command->kind >= MEDIA_ACTOR_COMMAND_OPEN &&
           command->kind <= MEDIA_ACTOR_COMMAND_SHUTDOWN &&
           ((!command->opaque_retain && !command->opaque_release) ||
            (command->opaque_retain && command->opaque_release));
}

static bool command_uses_reserved_slot(MediaActorCommandKind kind)
{
    return kind == MEDIA_ACTOR_COMMAND_STOP_ANY ||
           kind == MEDIA_ACTOR_COMMAND_RELEASE_LEASE ||
           kind == MEDIA_ACTOR_COMMAND_QUIESCE ||
           kind == MEDIA_ACTOR_COMMAND_SHUTDOWN;
}

static bool command_is_replaceable(MediaActorCommandKind kind)
{
    return kind == MEDIA_ACTOR_COMMAND_SEEK_MS ||
           kind == MEDIA_ACTOR_COMMAND_SET_VOLUME ||
           kind == MEDIA_ACTOR_COMMAND_SET_MUTE;
}

static bool command_can_replace(const MediaActorCommand *queued,
                                const MediaActorCommand *replacement)
{
    return queued && replacement && queued->kind == replacement->kind &&
           queued->producer == replacement->producer &&
           queued->session_token == replacement->session_token &&
           queued->generation == replacement->generation;
}

static void queue_push_locked(MediaActor *actor, MediaActorRequest *request)
{
    size_t tail = (actor->queue_head + actor->queue_count) %
                  actor->config.capacity;

    actor->queue[tail] = request;
    ++actor->queue_count;
    actor->health.pending = actor->queue_count;
    if (actor->queue_count > actor->health.max_depth)
        actor->health.max_depth = actor->queue_count;
}

static MediaActorRequest *queue_pop_locked(MediaActor *actor)
{
    MediaActorRequest *request;

    if (actor->queue_count == 0)
        return NULL;
    request = actor->queue[actor->queue_head];
    actor->queue[actor->queue_head] = NULL;
    actor->queue_head = (actor->queue_head + 1) % actor->config.capacity;
    --actor->queue_count;
    actor->health.pending = actor->queue_count;
    return request;
}

static MediaActorRequest *request_complete_locked(
    MediaActor *actor, MediaActorRequest *request,
    MediaActorSubmitStatus completion_status)
{
    request->completed = true;
    request->completion_status = completion_status;
    if (request->references > 0)
        --request->references;
    actor_wake_all_locked(actor);
    return request->references == 0 ? request : NULL;
}

static void actor_execute_request(MediaActor *actor,
                                  MediaActorRequest *request)
{
    MediaActorRequest *free_request;
    bool valid;
    bool ok;

    valid = !actor->config.validate ||
                 actor->config.validate(actor->config.context,
                                        &request->command);
    ok = valid && actor->config.execute(actor->config.context,
                                        &request->command);

    actor_lock(actor);
    if (!valid)
        ++actor->health.rejected_stale;
    else
        ++actor->health.executed;
    if (valid && !ok)
        ++actor->health.execution_failed;
    actor->health.last_completed_command_id = request->command.command_id;
    actor->health.current_command_id = 0;
    actor->health.current_command_kind = 0;
    actor->health.current_command_producer = MEDIA_ACTOR_PRODUCER_UNKNOWN;
    actor->health.current_session_token = 0;
    actor->health.current_generation = 0;
    actor->current_command_started_ms = 0;
    actor->last_heartbeat_ms = monotonic_ms();
    free_request = request_complete_locked(
        actor, request, !valid ? MEDIA_ACTOR_SUBMIT_STALE
                               : (ok ? MEDIA_ACTOR_SUBMIT_EXECUTED
                                     : MEDIA_ACTOR_SUBMIT_EXECUTION_FAILED));
    actor_unlock(actor);
    request_free(free_request);
}

static MediaActorRequest *actor_reject_pending_locked(MediaActor *actor)
{
    MediaActorRequest *free_list = NULL;
    MediaActorRequest *request;

    while ((request = queue_pop_locked(actor)) != NULL)
    {
        MediaActorRequest *free_request;

        ++actor->health.rejected_stopping;
        free_request = request_complete_locked(
            actor, request, MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN);
        if (free_request)
        {
            free_request->next_free = free_list;
            free_list = free_request;
        }
    }
    return free_list;
}

static bool actor_initialize_worker(MediaActor *actor)
{
    bool initialized = !actor->config.initialize ||
                       actor->config.initialize(actor->config.context);
    MediaActorRequest *free_list = NULL;

    actor_lock(actor);
    actor->health.initializing = false;
    actor->health.ready = initialized;
    actor->health.initialization_failed = !initialized;
    actor->health.dispatch_enabled = initialized &&
                                     actor->dispatch_requested;
    actor->last_heartbeat_ms = monotonic_ms();
    if (!initialized)
    {
        actor->running = false;
        actor->accepting = false;
        actor->health.running = false;
        actor->health.accepting = false;
        free_list = actor_reject_pending_locked(actor);
    }
    actor_wake_all_locked(actor);
    actor_unlock(actor);
    request_free_list(free_list);
    return initialized;
}

static void actor_finish_worker(MediaActor *actor)
{
    MediaActorRequest *free_list;

    actor_lock(actor);
    free_list = actor_reject_pending_locked(actor);
    actor_unlock(actor);
    request_free_list(free_list);

    if (actor->config.finalize)
        actor->config.finalize(actor->config.context);

    actor_lock(actor);
    actor->running = false;
    actor->accepting = false;
    actor->dispatch_requested = false;
    actor->worker_identity_valid = false;
    actor->health.running = false;
    actor->health.stopping = true;
    actor->health.accepting = false;
    actor->health.ready = false;
    actor->health.dispatch_enabled = false;
    actor->last_heartbeat_ms = monotonic_ms();
    actor_wake_all_locked(actor);
    actor_unlock(actor);
}

static void actor_worker_run(MediaActor *actor)
{
    if (!actor_initialize_worker(actor))
    {
        actor_lock(actor);
        actor->worker_identity_valid = false;
        actor_unlock(actor);
        return;
    }

    while (true)
    {
        size_t processed = 0;

        while (processed < actor->config.max_command_burst)
        {
            MediaActorRequest *request;

            actor_lock(actor);
            if (actor->stopping)
            {
                actor_unlock(actor);
                actor_finish_worker(actor);
                return;
            }
            if (!actor->health.dispatch_enabled)
            {
                actor_wait_locked(actor, actor->config.idle_poll_ms);
                actor_unlock(actor);
                break;
            }
            request = queue_pop_locked(actor);
            if (request)
            {
                actor->health.current_command_id = request->command.command_id;
                actor->health.current_command_kind = request->command.kind;
                actor->health.current_command_producer =
                    request->command.producer;
                actor->health.current_session_token =
                    request->command.session_token;
                actor->health.current_generation = request->command.generation;
                actor->current_command_started_ms = monotonic_ms();
                actor->last_heartbeat_ms = actor->current_command_started_ms;
            }
            actor_unlock(actor);
            if (!request)
                break;
            actor_execute_request(actor, request);
            ++processed;
        }

        if (actor->config.pump_events)
        {
            actor->config.pump_events(actor->config.context,
                                      processed ? 0 :
                                                  (int)actor->config.idle_poll_ms);
        }
        else if (processed == 0)
        {
            actor_lock(actor);
            if (!actor->stopping && actor->queue_count == 0)
                actor_wait_locked(actor, actor->config.idle_poll_ms);
            actor_unlock(actor);
        }
        actor_lock(actor);
        actor->last_heartbeat_ms = monotonic_ms();
        actor_unlock(actor);
    }
}

#ifdef __SWITCH__
static void actor_thread_main(void *opaque)
{
    MediaActor *actor = opaque;

    actor_lock(actor);
    actor->worker_handle = threadGetCurHandle();
    actor->worker_identity_valid = true;
    actor_unlock(actor);
    actor_worker_run(actor);
}
#else
static void *actor_thread_main(void *opaque)
{
    MediaActor *actor = opaque;

    actor_lock(actor);
    actor->worker_thread = pthread_self();
    actor->worker_identity_valid = true;
    actor_unlock(actor);
    actor_worker_run(actor);
    return NULL;
}
#endif

MediaActor *media_actor_create(const MediaActorConfig *config)
{
    MediaActor *actor;

    if (!config || !config->execute || config->capacity == 0 ||
        config->reserved_capacity >= config->capacity ||
        config->max_command_burst == 0 || config->idle_poll_ms == 0)
    {
        return NULL;
    }

    actor = calloc(1, sizeof(*actor));
    if (!actor)
        return NULL;
    actor->queue = calloc(config->capacity, sizeof(*actor->queue));
    if (!actor->queue)
    {
        free(actor);
        return NULL;
    }
    actor->config = *config;
#ifdef __SWITCH__
    mutexInit(&actor->mutex);
    condvarInit(&actor->cond);
#else
    if (pthread_mutex_init(&actor->mutex, NULL) != 0)
    {
        free(actor->queue);
        free(actor);
        return NULL;
    }
    if (pthread_cond_init(&actor->cond, NULL) != 0)
    {
        (void)pthread_mutex_destroy(&actor->mutex);
        free(actor->queue);
        free(actor);
        return NULL;
    }
#endif
    return actor;
}

bool media_actor_start(MediaActor *actor)
{
    if (!actor)
        return false;

    actor_lock(actor);
    if (actor->running)
    {
        actor_unlock(actor);
        return true;
    }
    if (actor->thread_started || actor->queue_count != 0)
    {
        actor_unlock(actor);
        return false;
    }
    actor->stopping = false;
    actor->accepting = true;
    actor->dispatch_requested = !actor->config.start_suspended;
    actor->running = true;
    actor->health.running = true;
    actor->health.stopping = false;
    actor->health.accepting = true;
    actor->health.initializing = actor->config.initialize != NULL;
    actor->health.ready = actor->config.initialize == NULL;
    actor->health.initialization_failed = false;
    actor->health.dispatch_enabled = actor->config.initialize == NULL &&
                                     actor->dispatch_requested;
    actor->last_heartbeat_ms = monotonic_ms();
    actor_unlock(actor);

#ifdef __SWITCH__
    if (R_FAILED(threadCreate(&actor->thread, actor_thread_main, actor, NULL,
                              actor->config.thread_stack_size,
                              actor->config.thread_priority,
                              actor->config.thread_core)))
    {
        actor_lock(actor);
        actor_mark_start_failed_locked(actor);
        actor_unlock(actor);
        return false;
    }
    if (R_FAILED(threadStart(&actor->thread)))
    {
        (void)threadClose(&actor->thread);
        actor_lock(actor);
        actor_mark_start_failed_locked(actor);
        actor_unlock(actor);
        return false;
    }
#else
    if (pthread_create(&actor->thread, NULL, actor_thread_main, actor) != 0)
    {
        actor_lock(actor);
        actor_mark_start_failed_locked(actor);
        actor_unlock(actor);
        return false;
    }
#endif
    actor_lock(actor);
    actor->thread_started = true;
    actor_unlock(actor);
    return true;
}

bool media_actor_activate(MediaActor *actor)
{
    bool active;

    if (!actor)
        return false;
    actor_lock(actor);
    active = actor->running && !actor->stopping &&
             !actor->health.initialization_failed;
    if (active)
    {
        actor->dispatch_requested = true;
        actor->health.dispatch_enabled = actor->health.ready;
        actor_wake_all_locked(actor);
    }
    actor_unlock(actor);
    if (active && actor->config.wakeup)
        actor->config.wakeup(actor->config.context);
    return active;
}

void media_actor_quiesce(MediaActor *actor)
{
    if (!actor)
        return;
    actor_lock(actor);
    actor->accepting = false;
    actor->health.accepting = false;
    actor_unlock(actor);
}

bool media_actor_wait_idle(MediaActor *actor, uint32_t timeout_ms)
{
    uint64_t deadline_ms = 0;
    bool idle;

    if (!actor)
        return false;
    if (timeout_ms != MEDIA_ACTOR_WAIT_FOREVER)
        deadline_ms = monotonic_ms() + timeout_ms;

    actor_lock(actor);
    while (actor->queue_count > 0 || actor->health.current_command_id != 0)
    {
        uint32_t remaining_ms = MEDIA_ACTOR_WAIT_FOREVER;

        if (timeout_ms != MEDIA_ACTOR_WAIT_FOREVER)
        {
            uint64_t now_ms = monotonic_ms();

            if (now_ms >= deadline_ms)
                break;
            remaining_ms = (uint32_t)(deadline_ms - now_ms);
        }
        actor_wait_locked(actor, remaining_ms);
    }
    idle = actor->queue_count == 0 && actor->health.current_command_id == 0;
    actor_unlock(actor);
    return idle;
}

void media_actor_stop(MediaActor *actor)
{
    bool thread_started;

    if (!actor)
        return;
    if (media_actor_is_current_thread(actor))
    {
        actor_lock(actor);
        actor->stopping = true;
        actor->accepting = false;
        actor->health.stopping = true;
        actor->health.accepting = false;
        actor_wake_all_locked(actor);
        actor_unlock(actor);
        return;
    }
    actor_lock(actor);
    while (actor->join_in_progress)
        actor_wait_locked(actor, MEDIA_ACTOR_WAIT_FOREVER);
    thread_started = actor->thread_started;
    if (!thread_started)
    {
        actor_unlock(actor);
        return;
    }
    actor->join_in_progress = true;
    actor->stopping = true;
    actor->accepting = false;
    actor->health.stopping = true;
    actor->health.accepting = false;
    actor_wake_all_locked(actor);
    actor_unlock(actor);

    if (actor->config.wakeup)
        actor->config.wakeup(actor->config.context);
#ifdef __SWITCH__
    (void)threadWaitForExit(&actor->thread);
    (void)threadClose(&actor->thread);
#else
    (void)pthread_join(actor->thread, NULL);
#endif

    actor_lock(actor);
    actor->thread_started = false;
    actor->join_in_progress = false;
    actor->running = false;
    actor->health.running = false;
    actor_wake_all_locked(actor);
    actor_unlock(actor);
}

void media_actor_destroy(MediaActor *actor)
{
    MediaActorRequest *free_list;

    if (!actor)
        return;
    media_actor_stop(actor);
    actor_lock(actor);
    free_list = actor_reject_pending_locked(actor);
    actor_unlock(actor);
    request_free_list(free_list);
#ifndef __SWITCH__
    (void)pthread_cond_destroy(&actor->cond);
    (void)pthread_mutex_destroy(&actor->mutex);
#endif
    free(actor->queue);
    free(actor);
}

static MediaActorSubmitStatus actor_submit(
    MediaActor *actor, const MediaActorCommand *command, bool wait,
    uint32_t timeout_ms)
{
    MediaActorRequest *request;
    MediaActorSubmitStatus status;
    MediaActorRequest *replaced_request = NULL;
    size_t normal_capacity;
    uint64_t deadline_ms = 0;

    if (!actor || !command_valid(command))
        return MEDIA_ACTOR_SUBMIT_INVALID;

    if (wait && media_actor_is_current_thread(actor))
    {
        MediaActorCommand direct = *command;
        bool ok;

        actor_lock(actor);
        if (!actor->running)
        {
            actor_unlock(actor);
            return MEDIA_ACTOR_SUBMIT_NOT_RUNNING;
        }
        if (actor->stopping)
        {
            ++actor->health.rejected_stopping;
            actor_unlock(actor);
            return MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN;
        }
        if (!actor->accepting)
        {
            ++actor->health.rejected_stopping;
            actor_unlock(actor);
            return MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN;
        }
        direct.command_id = ++actor->health.last_command_id;
        ++actor->health.submitted;
        actor_unlock(actor);

        if (actor->config.validate &&
            !actor->config.validate(actor->config.context, &direct))
        {
        actor_lock(actor);
        ++actor->health.rejected_stale;
        actor->health.last_completed_command_id = direct.command_id;
        actor->last_heartbeat_ms = monotonic_ms();
        actor_unlock(actor);
            return MEDIA_ACTOR_SUBMIT_STALE;
        }
        ok = actor->config.execute(actor->config.context, &direct);
        actor_lock(actor);
        ++actor->health.executed;
    if (!ok)
        ++actor->health.execution_failed;
    actor->health.last_completed_command_id = direct.command_id;
    actor->last_heartbeat_ms = monotonic_ms();
    actor_unlock(actor);
        return ok ? MEDIA_ACTOR_SUBMIT_EXECUTED
                  : MEDIA_ACTOR_SUBMIT_EXECUTION_FAILED;
    }

    request = request_create(command);
    if (!request)
        return MEDIA_ACTOR_SUBMIT_NO_MEMORY;

    actor_lock(actor);
    if (!actor->running)
    {
        status = MEDIA_ACTOR_SUBMIT_NOT_RUNNING;
        actor_unlock(actor);
        request_free(request);
        return status;
    }
    if (actor->stopping || !actor->accepting)
    {
        ++actor->health.rejected_stopping;
        actor_unlock(actor);
        request_free(request);
        return MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN;
    }

    if (!wait && command_is_replaceable(command->kind) &&
        actor->queue_count > 0)
    {
        size_t tail = (actor->queue_head + actor->queue_count - 1u) %
                      actor->config.capacity;
        MediaActorRequest *queued = actor->queue[tail];

        if (queued->references == 1u &&
            command_can_replace(&queued->command, command))
        {
            request->command.command_id = ++actor->health.last_command_id;
            request->references = 1u;
            ++actor->health.submitted;
            ++actor->health.coalesced;
            actor->queue[tail] = request;
            replaced_request = request_complete_locked(
                actor, queued, MEDIA_ACTOR_SUBMIT_ACCEPTED);
            actor_unlock(actor);
            request_free(replaced_request);
            if (actor->config.wakeup)
                actor->config.wakeup(actor->config.context);
            return MEDIA_ACTOR_SUBMIT_ACCEPTED;
        }
    }

    normal_capacity = actor->config.capacity -
                      actor->config.reserved_capacity;
    if (actor->queue_count >= actor->config.capacity ||
        (!command_uses_reserved_slot(command->kind) &&
         actor->queue_count >= normal_capacity))
    {
        ++actor->health.rejected_full;
        actor_unlock(actor);
        request_free(request);
        return MEDIA_ACTOR_SUBMIT_QUEUE_FULL;
    }

    request->command.command_id = ++actor->health.last_command_id;
    request->references = wait ? 2u : 1u;
    ++actor->health.submitted;
    queue_push_locked(actor, request);
    actor_wake_all_locked(actor);
    actor_unlock(actor);

    if (actor->config.wakeup)
        actor->config.wakeup(actor->config.context);
    if (!wait)
        return MEDIA_ACTOR_SUBMIT_ACCEPTED;

    if (timeout_ms != MEDIA_ACTOR_WAIT_FOREVER)
        deadline_ms = monotonic_ms() + timeout_ms;

    actor_lock(actor);
    while (!request->completed)
    {
        uint32_t remaining_ms = MEDIA_ACTOR_WAIT_FOREVER;

        if (timeout_ms != MEDIA_ACTOR_WAIT_FOREVER)
        {
            uint64_t now_ms = monotonic_ms();

            if (now_ms >= deadline_ms)
                break;
            remaining_ms = (uint32_t)(deadline_ms - now_ms);
        }
        actor_wait_locked(actor, remaining_ms);
    }

    if (request->completed)
    {
        status = request->completion_status;
    }
    else
    {
        ++actor->health.timed_out;
        status = MEDIA_ACTOR_SUBMIT_TIMEOUT;
    }
    if (request->references > 0)
        --request->references;
    if (request->references != 0)
        request = NULL;
    actor_unlock(actor);
    request_free(request);
    return status;
}

MediaActorSubmitStatus media_actor_submit_async(
    MediaActor *actor, const MediaActorCommand *command)
{
    return actor_submit(actor, command, false, 0);
}

MediaActorSubmitStatus media_actor_submit_wait(
    MediaActor *actor, const MediaActorCommand *command, uint32_t timeout_ms)
{
    return actor_submit(actor, command, true, timeout_ms);
}

bool media_actor_is_current_thread(MediaActor *actor)
{
    bool is_current = false;

    if (!actor)
        return false;
    actor_lock(actor);
    if (actor->worker_identity_valid)
    {
#ifdef __SWITCH__
        is_current = actor->worker_handle == threadGetCurHandle();
#else
        is_current = pthread_equal(actor->worker_thread, pthread_self()) != 0;
#endif
    }
    actor_unlock(actor);
    return is_current;
}

bool media_actor_get_health(MediaActor *actor, MediaActorHealth *out)
{
    uint64_t now_ms;

    if (!actor || !out)
        return false;
    actor_lock(actor);
    *out = actor->health;
    now_ms = monotonic_ms();
    out->current_command_age_ms = actor->current_command_started_ms > 0 &&
                                          now_ms >= actor->current_command_started_ms
                                      ? now_ms - actor->current_command_started_ms
                                      : 0;
    out->heartbeat_age_ms = actor->last_heartbeat_ms > 0 &&
                                    now_ms >= actor->last_heartbeat_ms
                                ? now_ms - actor->last_heartbeat_ms
                                : 0;
    actor_unlock(actor);
    return true;
}

const char *media_actor_submit_status_name(MediaActorSubmitStatus status)
{
    switch (status)
    {
    case MEDIA_ACTOR_SUBMIT_ACCEPTED:
        return "accepted";
    case MEDIA_ACTOR_SUBMIT_EXECUTED:
        return "executed";
    case MEDIA_ACTOR_SUBMIT_EXECUTION_FAILED:
        return "execution-failed";
    case MEDIA_ACTOR_SUBMIT_INVALID:
        return "invalid";
    case MEDIA_ACTOR_SUBMIT_NOT_RUNNING:
        return "not-running";
    case MEDIA_ACTOR_SUBMIT_QUEUE_FULL:
        return "queue-full";
    case MEDIA_ACTOR_SUBMIT_NO_MEMORY:
        return "no-memory";
    case MEDIA_ACTOR_SUBMIT_TIMEOUT:
        return "timeout";
    case MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN:
        return "shutting-down";
    case MEDIA_ACTOR_SUBMIT_STALE:
        return "stale";
    default:
        return "unknown";
    }
}
