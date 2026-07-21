#define _POSIX_C_SOURCE 200809L

#include "player/core/media_actor.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_SIZE(values) (sizeof(values) / sizeof((values)[0]))
#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            return false;                                                       \
        }                                                                       \
    } while (0)

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool block_next;
    bool executor_blocked;
    bool release_executor;
    bool executor_thread_set;
    pthread_t executor_thread;
    bool wrong_executor;
    uint64_t command_ids[256];
    int values[256];
    size_t command_count;
    char captured_text[128];
    char captured_metadata[128];
    bool require_validation;
    MediaActorProducer valid_producer;
    uint64_t valid_token;
    uint32_t valid_generation;
    MediaActor *actor;
    bool initialize_blocked;
    bool release_initialize;
    bool initialize_result;
    bool initialize_called;
    bool finalize_called;
    bool lifecycle_wrong_executor;
} FakeBackend;

typedef struct
{
    MediaActor *actor;
    int first_value;
    int count;
    bool ok;
} ProducerArgs;

typedef struct
{
    MediaActor *actor;
    MediaActorSubmitStatus status;
} WaiterArgs;

typedef struct
{
    pthread_mutex_t mutex;
    int retain_count;
    int release_count;
} RefObject;

static void ref_object_retain(void *opaque)
{
    RefObject *object = opaque;

    pthread_mutex_lock(&object->mutex);
    ++object->retain_count;
    pthread_mutex_unlock(&object->mutex);
}

static void ref_object_release(void *opaque)
{
    RefObject *object = opaque;

    pthread_mutex_lock(&object->mutex);
    ++object->release_count;
    pthread_mutex_unlock(&object->mutex);
}

static void sleep_ms(unsigned int milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };

    while (nanosleep(&duration, &duration) != 0)
    {
    }
}

static void fake_backend_init(FakeBackend *backend)
{
    memset(backend, 0, sizeof(*backend));
    backend->initialize_result = true;
    (void)pthread_mutex_init(&backend->mutex, NULL);
    (void)pthread_cond_init(&backend->cond, NULL);
}

static bool fake_initialize(void *context)
{
    FakeBackend *backend = context;
    bool result;

    pthread_mutex_lock(&backend->mutex);
    backend->initialize_called = true;
    if (!media_actor_is_current_thread(backend->actor))
        backend->lifecycle_wrong_executor = true;
    pthread_cond_broadcast(&backend->cond);
    while (backend->initialize_blocked && !backend->release_initialize)
        pthread_cond_wait(&backend->cond, &backend->mutex);
    result = backend->initialize_result;
    pthread_mutex_unlock(&backend->mutex);
    return result;
}

static void fake_finalize(void *context)
{
    FakeBackend *backend = context;

    pthread_mutex_lock(&backend->mutex);
    backend->finalize_called = true;
    if (!media_actor_is_current_thread(backend->actor))
        backend->lifecycle_wrong_executor = true;
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);
}

static void fake_backend_destroy(FakeBackend *backend)
{
    (void)pthread_cond_destroy(&backend->cond);
    (void)pthread_mutex_destroy(&backend->mutex);
}

static bool fake_execute(void *context, const MediaActorCommand *command)
{
    FakeBackend *backend = context;

    pthread_mutex_lock(&backend->mutex);
    if (!backend->executor_thread_set)
    {
        backend->executor_thread = pthread_self();
        backend->executor_thread_set = true;
    }
    else if (!pthread_equal(backend->executor_thread, pthread_self()))
    {
        backend->wrong_executor = true;
    }

    if (!media_actor_is_current_thread(backend->actor))
        backend->wrong_executor = true;

    if (backend->block_next)
    {
        backend->block_next = false;
        backend->executor_blocked = true;
        pthread_cond_broadcast(&backend->cond);
        while (!backend->release_executor)
            pthread_cond_wait(&backend->cond, &backend->mutex);
    }

    if (backend->command_count < ARRAY_SIZE(backend->command_ids))
    {
        size_t index = backend->command_count++;
        backend->command_ids[index] = command->command_id;
        backend->values[index] = command->value;
    }
    if (command->text)
        snprintf(backend->captured_text, sizeof(backend->captured_text), "%s", command->text);
    if (command->metadata)
        snprintf(backend->captured_metadata, sizeof(backend->captured_metadata), "%s", command->metadata);
    pthread_mutex_unlock(&backend->mutex);

    return command->value != -999;
}

static bool fake_validate(void *context, const MediaActorCommand *command)
{
    FakeBackend *backend = context;
    bool valid;

    pthread_mutex_lock(&backend->mutex);
    valid = !backend->require_validation ||
            (command->producer == backend->valid_producer &&
             command->session_token == backend->valid_token &&
             command->generation == backend->valid_generation);
    pthread_mutex_unlock(&backend->mutex);
    return valid;
}

static void fake_pump_events(void *context, int timeout_ms)
{
    (void)context;
    if (timeout_ms > 0)
        sleep_ms((unsigned int)timeout_ms);
}

static void fake_wakeup(void *context)
{
    (void)context;
}

static MediaActor *create_actor(FakeBackend *backend, size_t capacity,
                                size_t reserved_capacity)
{
    MediaActorConfig config = {
        .capacity = capacity,
        .reserved_capacity = reserved_capacity,
        .max_command_burst = 8,
        .idle_poll_ms = 1,
        .thread_stack_size = 0x8000,
        .thread_priority = 0x2B,
        .thread_core = -2,
        .validate = fake_validate,
        .execute = fake_execute,
        .pump_events = fake_pump_events,
        .wakeup = fake_wakeup,
        .context = backend,
    };
    MediaActor *actor = media_actor_create(&config);

    backend->actor = actor;
    return actor;
}

static MediaActor *create_lifecycle_actor(FakeBackend *backend,
                                          bool start_suspended)
{
    MediaActorConfig config = {
        .capacity = 8,
        .reserved_capacity = 2,
        .max_command_burst = 8,
        .idle_poll_ms = 1,
        .thread_stack_size = 0x8000,
        .thread_priority = 0x2B,
        .thread_core = -2,
        .start_suspended = start_suspended,
        .initialize = fake_initialize,
        .finalize = fake_finalize,
        .validate = fake_validate,
        .execute = fake_execute,
        .pump_events = fake_pump_events,
        .wakeup = fake_wakeup,
        .context = backend,
    };
    MediaActor *actor = media_actor_create(&config);

    backend->actor = actor;
    return actor;
}

static void wait_until_blocked(FakeBackend *backend)
{
    pthread_mutex_lock(&backend->mutex);
    while (!backend->executor_blocked)
        pthread_cond_wait(&backend->cond, &backend->mutex);
    pthread_mutex_unlock(&backend->mutex);
}

static void release_executor(FakeBackend *backend)
{
    pthread_mutex_lock(&backend->mutex);
    backend->release_executor = true;
    pthread_cond_broadcast(&backend->cond);
    pthread_mutex_unlock(&backend->mutex);
}

static bool wait_for_executed(MediaActor *actor, uint64_t expected)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000; ++attempt)
    {
        MediaActorHealth health;

        if (media_actor_get_health(actor, &health) && health.executed >= expected)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool wait_for_pending(MediaActor *actor, size_t expected)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000; ++attempt)
    {
        MediaActorHealth health;

        if (media_actor_get_health(actor, &health) && health.pending >= expected)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool wait_for_ready(MediaActor *actor, bool failed)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000; ++attempt)
    {
        MediaActorHealth health;

        if (media_actor_get_health(actor, &health) &&
            (failed ? health.initialization_failed : health.ready))
        {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

static void *producer_main(void *opaque)
{
    ProducerArgs *args = opaque;
    int i;

    args->ok = true;
    for (i = 0; i < args->count; ++i)
    {
        MediaActorCommand command = {
            .kind = MEDIA_ACTOR_COMMAND_SEEK_MS,
            .producer = MEDIA_ACTOR_PRODUCER_TEST,
            .value = args->first_value + i,
        };
        MediaActorSubmitStatus status = media_actor_submit_wait(
            args->actor, &command, MEDIA_ACTOR_WAIT_FOREVER);
        if (status != MEDIA_ACTOR_SUBMIT_EXECUTED)
        {
            args->ok = false;
            break;
        }
    }
    return NULL;
}

static void *waiter_main(void *opaque)
{
    WaiterArgs *args = opaque;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 77,
    };

    args->status = media_actor_submit_wait(args->actor, &command,
                                           MEDIA_ACTOR_WAIT_FOREVER);
    return NULL;
}

static void *stopper_main(void *opaque)
{
    media_actor_stop(opaque);
    return NULL;
}

static bool test_single_executor_and_fifo(void)
{
    FakeBackend backend;
    MediaActor *actor;
    ProducerArgs args[4];
    pthread_t producers[4];
    size_t i;

    fake_backend_init(&backend);
    actor = create_actor(&backend, 64, 4);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));

    for (i = 0; i < ARRAY_SIZE(producers); ++i)
    {
        args[i].actor = actor;
        args[i].first_value = (int)i * 100;
        args[i].count = 20;
        args[i].ok = false;
        CHECK(pthread_create(&producers[i], NULL, producer_main, &args[i]) == 0);
    }
    for (i = 0; i < ARRAY_SIZE(producers); ++i)
    {
        CHECK(pthread_join(producers[i], NULL) == 0);
        CHECK(args[i].ok);
    }

    media_actor_stop(actor);
    CHECK(!backend.wrong_executor);
    CHECK(backend.command_count == 80);
    for (i = 1; i < backend.command_count; ++i)
        CHECK(backend.command_ids[i] > backend.command_ids[i - 1]);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_deep_copy_and_saturation(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_OPEN,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 1,
    };
    char uri[] = "https://example.invalid/original.m3u8";
    char metadata[] = "original metadata";
    size_t i;

    fake_backend_init(&backend);
    backend.block_next = true;
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));

    command.text = uri;
    command.metadata = metadata;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    wait_until_blocked(&backend);
    memcpy(uri, "changed", sizeof("changed"));
    memcpy(metadata, "changed", sizeof("changed"));

    command.kind = MEDIA_ACTOR_COMMAND_PLAY;
    command.text = NULL;
    command.metadata = NULL;
    for (i = 0; i < 6; ++i)
    {
        command.value = (int)i + 10;
        CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    }
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_QUEUE_FULL);

    command.kind = MEDIA_ACTOR_COMMAND_STOP_ANY;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_QUEUE_FULL);

    release_executor(&backend);
    media_actor_stop(actor);
    CHECK(strcmp(backend.captured_text, "https://example.invalid/original.m3u8") == 0);
    CHECK(strcmp(backend.captured_metadata, "original metadata") == 0);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_timeout_lifetime(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 1,
    };
    MediaActorHealth health;

    fake_backend_init(&backend);
    backend.block_next = true;
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    wait_until_blocked(&backend);

    command.value = 2;
    CHECK(media_actor_submit_wait(actor, &command, 10) == MEDIA_ACTOR_SUBMIT_TIMEOUT);
    release_executor(&backend);
    CHECK(wait_for_executed(actor, 2));
    media_actor_stop(actor);
    CHECK(media_actor_get_health(actor, &health));
    CHECK(health.timed_out == 1);
    CHECK(health.executed == 2);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_stop_rejects_pending_waiter(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 1,
    };
    WaiterArgs waiter = {0};
    pthread_t waiter_thread;
    pthread_t stopper_thread;

    fake_backend_init(&backend);
    backend.block_next = true;
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    wait_until_blocked(&backend);

    waiter.actor = actor;
    CHECK(pthread_create(&waiter_thread, NULL, waiter_main, &waiter) == 0);
    CHECK(wait_for_pending(actor, 1));
    CHECK(pthread_create(&stopper_thread, NULL, stopper_main, actor) == 0);
    sleep_ms(5);
    release_executor(&backend);

    CHECK(pthread_join(waiter_thread, NULL) == 0);
    CHECK(pthread_join(stopper_thread, NULL) == 0);
    CHECK(waiter.status == MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_concurrent_stop_is_idempotent(void)
{
    FakeBackend backend;
    MediaActor *actor;
    pthread_t stoppers[2];
    size_t i;

    fake_backend_init(&backend);
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    for (i = 0; i < ARRAY_SIZE(stoppers); ++i)
        CHECK(pthread_create(&stoppers[i], NULL, stopper_main, actor) == 0);
    for (i = 0; i < ARRAY_SIZE(stoppers); ++i)
        CHECK(pthread_join(stoppers[i], NULL) == 0);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_stale_generation_is_rejected(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 1,
    };
    MediaActorHealth health;

    fake_backend_init(&backend);
    backend.block_next = true;
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    wait_until_blocked(&backend);

    pthread_mutex_lock(&backend.mutex);
    backend.require_validation = true;
    backend.valid_producer = MEDIA_ACTOR_PRODUCER_IPTV;
    backend.valid_token = 20;
    backend.valid_generation = 2;
    pthread_mutex_unlock(&backend.mutex);

    command.producer = MEDIA_ACTOR_PRODUCER_IPTV;
    command.session_token = 10;
    command.generation = 1;
    command.value = 10;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    command.session_token = 20;
    command.generation = 2;
    command.value = 20;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);

    release_executor(&backend);
    CHECK(wait_for_executed(actor, 2));
    media_actor_stop(actor);
    CHECK(media_actor_get_health(actor, &health));
    CHECK(health.rejected_stale == 1);
    CHECK(backend.command_count == 2);
    CHECK(backend.values[0] == 1);
    CHECK(backend.values[1] == 20);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_opaque_payload_lifetime(void)
{
    FakeBackend backend;
    RefObject object;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_BIND_STREAM,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .opaque = &object,
        .opaque_retain = ref_object_retain,
        .opaque_release = ref_object_release,
    };
    int retain_count;
    int release_count;

    fake_backend_init(&backend);
    memset(&object, 0, sizeof(object));
    CHECK(pthread_mutex_init(&object.mutex, NULL) == 0);
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    CHECK(wait_for_executed(actor, 1));
    media_actor_stop(actor);

    pthread_mutex_lock(&object.mutex);
    retain_count = object.retain_count;
    release_count = object.release_count;
    pthread_mutex_unlock(&object.mutex);
    CHECK(retain_count == 1);
    CHECK(release_count == 1);
    media_actor_destroy(actor);
    CHECK(pthread_mutex_destroy(&object.mutex) == 0);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_health_coalescing_and_quiesce(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 1,
    };
    MediaActorHealth health;

    fake_backend_init(&backend);
    backend.block_next = true;
    actor = create_actor(&backend, 8, 2);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    wait_until_blocked(&backend);
    sleep_ms(5);
    CHECK(media_actor_get_health(actor, &health));
    CHECK(health.current_command_id != 0);
    CHECK(health.current_command_kind == MEDIA_ACTOR_COMMAND_PLAY);
    CHECK(health.current_command_producer == MEDIA_ACTOR_PRODUCER_TEST);
    CHECK(health.current_command_age_ms >= 1);
    CHECK(health.heartbeat_age_ms >= 1);

    command.kind = MEDIA_ACTOR_COMMAND_SEEK_MS;
    command.value = 10;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    command.value = 20;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    command.value = 30;
    CHECK(media_actor_submit_async(actor, &command) == MEDIA_ACTOR_SUBMIT_ACCEPTED);
    CHECK(media_actor_get_health(actor, &health));
    CHECK(health.pending == 1);
    CHECK(health.coalesced == 2);

    media_actor_quiesce(actor);
    CHECK(media_actor_get_health(actor, &health));
    CHECK(!health.accepting);
    CHECK(media_actor_submit_async(actor, &command) ==
          MEDIA_ACTOR_SUBMIT_SHUTTING_DOWN);
    release_executor(&backend);
    CHECK(wait_for_executed(actor, 2));
    media_actor_stop(actor);
    CHECK(backend.command_count == 2);
    CHECK(backend.values[1] == 30);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_async_initialize_activate_finalize(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
        .value = 42,
    };
    MediaActorHealth health;

    fake_backend_init(&backend);
    backend.initialize_blocked = true;
    actor = create_lifecycle_actor(&backend, true);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));

    pthread_mutex_lock(&backend.mutex);
    while (!backend.initialize_called)
        pthread_cond_wait(&backend.cond, &backend.mutex);
    pthread_mutex_unlock(&backend.mutex);
    CHECK(media_actor_submit_async(actor, &command) ==
          MEDIA_ACTOR_SUBMIT_ACCEPTED);
    CHECK(wait_for_pending(actor, 1));
    CHECK(media_actor_get_health(actor, &health));
    CHECK(health.initializing);
    CHECK(!health.ready);
    CHECK(!health.dispatch_enabled);

    pthread_mutex_lock(&backend.mutex);
    backend.release_initialize = true;
    pthread_cond_broadcast(&backend.cond);
    pthread_mutex_unlock(&backend.mutex);
    CHECK(wait_for_ready(actor, false));
    sleep_ms(5);
    CHECK(backend.command_count == 0);
    CHECK(media_actor_activate(actor));
    CHECK(wait_for_executed(actor, 1));
    media_actor_stop(actor);
    CHECK(backend.finalize_called);
    CHECK(!backend.lifecycle_wrong_executor);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

static bool test_initialize_failure_rejects_queued_work(void)
{
    FakeBackend backend;
    MediaActor *actor;
    MediaActorCommand command = {
        .kind = MEDIA_ACTOR_COMMAND_PLAY,
        .producer = MEDIA_ACTOR_PRODUCER_TEST,
    };
    MediaActorHealth health;

    fake_backend_init(&backend);
    backend.initialize_blocked = true;
    backend.initialize_result = false;
    actor = create_lifecycle_actor(&backend, true);
    CHECK(actor != NULL);
    CHECK(media_actor_start(actor));

    pthread_mutex_lock(&backend.mutex);
    while (!backend.initialize_called)
        pthread_cond_wait(&backend.cond, &backend.mutex);
    pthread_mutex_unlock(&backend.mutex);
    CHECK(media_actor_submit_async(actor, &command) ==
          MEDIA_ACTOR_SUBMIT_ACCEPTED);
    pthread_mutex_lock(&backend.mutex);
    backend.release_initialize = true;
    pthread_cond_broadcast(&backend.cond);
    pthread_mutex_unlock(&backend.mutex);

    CHECK(wait_for_ready(actor, true));
    CHECK(media_actor_get_health(actor, &health));
    CHECK(!health.running);
    CHECK(!health.accepting);
    CHECK(health.pending == 0);
    CHECK(health.rejected_stopping == 1);
    CHECK(media_actor_submit_async(actor, &command) ==
          MEDIA_ACTOR_SUBMIT_NOT_RUNNING);
    media_actor_stop(actor);
    CHECK(!backend.finalize_called);
    CHECK(!backend.lifecycle_wrong_executor);

    media_actor_destroy(actor);
    fake_backend_destroy(&backend);
    return true;
}

int main(void)
{
    if (!test_single_executor_and_fifo() ||
        !test_deep_copy_and_saturation() ||
        !test_timeout_lifetime() ||
        !test_stop_rejects_pending_waiter() ||
        !test_concurrent_stop_is_idempotent() ||
        !test_stale_generation_is_rejected() ||
        !test_opaque_payload_lifetime() ||
        !test_health_coalescing_and_quiesce() ||
        !test_async_initialize_activate_finalize() ||
        !test_initialize_failure_rejects_queued_work())
    {
        return 1;
    }
    puts("media actor tests passed");
    return 0;
}
