#include "app/protocol_coordinator.h"

#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
    PlayerMediaOwner owner;
    uint64_t token;
    bool acquired;
} ClaimThread;

typedef struct
{
    pthread_mutex_t mutex;
    char events[32];
    size_t event_count;
    bool iptv_result;
    bool dlna_result;
    bool airplay_result;
    bool background_fail_once;
    unsigned background_calls;
    ProtocolAirPlayStatus airplay;
} FakeRuntime;

typedef struct
{
    atomic_bool iptv_started;
    atomic_bool iptv_finished;
    atomic_bool dlna_started;
    atomic_bool airplay_started;
    atomic_bool airplay_cancelled;
    atomic_int stop_count;
} DelayedRuntime;

typedef struct
{
    atomic_int airplay_start_count;
    atomic_bool airplay_restart_started;
    atomic_bool airplay_cancelled;
    atomic_int stop_count;
    atomic_int background_calls;
    atomic_bool background_fail_once;
} ResourceStopRuntime;

typedef struct
{
    PlayerOwnershipLease lease;
    atomic_bool entered;
    bool result;
} ResourceWaitThread;

static ProtocolCoordinatorSnapshot snapshot(void);
static bool wait_for_service_state(ProtocolService service,
                                   ProtocolServiceState expected);
static bool wait_for_resource_mode(ProtocolResourceMode expected);
static bool resource_immediate_start(void *context);
static bool resource_cancellable_airplay_start(void *context);
static void resource_request_stop(void *context);
static void resource_stop(void *context);
static bool resource_background_suspend(void *context, bool suspended);

static uint64_t monotonic_ms(void)
{
    struct timespec now;

    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void sleep_ms(unsigned int duration_ms)
{
    struct timespec duration = {
        .tv_sec = (time_t)(duration_ms / 1000u),
        .tv_nsec = (long)(duration_ms % 1000u) * 1000000L,
    };

    while (nanosleep(&duration, &duration) != 0)
    {
    }
}

static bool delayed_iptv_start(void *context)
{
    DelayedRuntime *runtime = context;

    atomic_store(&runtime->iptv_started, true);
    sleep_ms(150);
    atomic_store(&runtime->iptv_finished, true);
    return true;
}

static bool delayed_dlna_start(void *context)
{
    DelayedRuntime *runtime = context;

    atomic_store(&runtime->dlna_started, true);
    return true;
}

static void delayed_stop(void *context)
{
    DelayedRuntime *runtime = context;

    atomic_fetch_add(&runtime->stop_count, 1);
}

static bool cancellable_airplay_start(void *context)
{
    DelayedRuntime *runtime = context;

    atomic_store(&runtime->airplay_started, true);
    while (!atomic_load(&runtime->airplay_cancelled))
        sleep_ms(1u);
    return true;
}

static void cancellable_airplay_request_stop(void *context)
{
    DelayedRuntime *runtime = context;

    atomic_store(&runtime->airplay_cancelled, true);
}

static bool delayed_airplay_get_status(void *context,
                                       ProtocolAirPlayStatus *status_out)
{
    (void)context;
    (void)status_out;
    return false;
}

static void test_stop_cancels_start_worker_before_join(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {false, false, true}
    };
    DelayedRuntime runtime;
    ProtocolCoordinatorOperations operations = {
        .context = &runtime,
        .services = {
            [PROTOCOL_SERVICE_AIRPLAY] = {
                .start = cancellable_airplay_start,
                .request_stop = cancellable_airplay_request_stop,
                .stop = delayed_stop,
            },
        },
        .airplay_get_status = delayed_airplay_get_status,
    };
    unsigned int attempt;

    memset(&runtime, 0, sizeof(runtime));
    atomic_init(&runtime.airplay_started, false);
    atomic_init(&runtime.airplay_cancelled, false);
    atomic_init(&runtime.stop_count, 0);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    for (attempt = 0; attempt < 1000u &&
                      !atomic_load(&runtime.airplay_started); ++attempt)
        sleep_ms(1u);
    assert(atomic_load(&runtime.airplay_started));
    protocol_coordinator_stop();
    assert(atomic_load(&runtime.airplay_cancelled));
    assert(atomic_load(&runtime.stop_count) == 1);
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPED);
}

static void *resource_wait_thread_main(void *argument)
{
    ResourceWaitThread *waiter = argument;

    atomic_store(&waiter->entered, true);
    waiter->result = protocol_coordinator_media_wait_resources(
        &waiter->lease, UINT32_MAX);
    return NULL;
}

static void test_stop_cancels_resource_transition_before_join(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    ResourceStopRuntime runtime;
    ProtocolCoordinatorOperations operations = {
        .context = &runtime,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = resource_immediate_start,
                .stop = resource_stop,
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = resource_immediate_start,
                .stop = resource_stop,
            },
            [PROTOCOL_SERVICE_AIRPLAY] = {
                .start = resource_cancellable_airplay_start,
                .request_stop = resource_request_stop,
                .stop = resource_stop,
            },
        },
        .airplay_get_status = delayed_airplay_get_status,
        .set_background_network_suspended = resource_background_suspend,
    };
    ProtocolMediaTransaction dlna;
    unsigned int attempt;

    atomic_init(&runtime.airplay_start_count, 0);
    atomic_init(&runtime.airplay_restart_started, false);
    atomic_init(&runtime.airplay_cancelled, false);
    atomic_init(&runtime.stop_count, 0);
    atomic_init(&runtime.background_calls, 0);
    atomic_init(&runtime.background_fail_once, false);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(!protocol_coordinator_media_wait_resources(&dlna.lease, 2u));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE));
    assert(protocol_coordinator_media_wait_resources(&dlna.lease, 2u));
    assert(protocol_coordinator_media_release(&dlna.lease));
    for (attempt = 0; attempt < 1000u &&
                      !atomic_load(&runtime.airplay_restart_started); ++attempt)
    {
        protocol_coordinator_tick();
        sleep_ms(1u);
    }
    assert(atomic_load(&runtime.airplay_restart_started));
    protocol_coordinator_stop();
    assert(atomic_load(&runtime.airplay_cancelled));
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPED);
}

static void test_reclaim_cancels_superseded_home_restart(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    ResourceStopRuntime runtime;
    ProtocolCoordinatorOperations operations = {
        .context = &runtime,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = resource_immediate_start,
                .stop = resource_stop,
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = resource_immediate_start,
                .stop = resource_stop,
            },
            [PROTOCOL_SERVICE_AIRPLAY] = {
                .start = resource_cancellable_airplay_start,
                .request_stop = resource_request_stop,
                .stop = resource_stop,
            },
        },
        .airplay_get_status = delayed_airplay_get_status,
        .set_background_network_suspended = resource_background_suspend,
    };
    ProtocolMediaTransaction first;
    ProtocolMediaTransaction second;
    unsigned int attempt;

    atomic_init(&runtime.airplay_start_count, 0);
    atomic_init(&runtime.airplay_restart_started, false);
    atomic_init(&runtime.airplay_cancelled, false);
    atomic_init(&runtime.stop_count, 0);
    atomic_init(&runtime.background_calls, 0);
    atomic_init(&runtime.background_fail_once, false);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &first));
    protocol_coordinator_media_end(&first);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE));
    assert(protocol_coordinator_media_release(&first.lease));
    for (attempt = 0; attempt < 1000u &&
                      !atomic_load(&runtime.airplay_restart_started); ++attempt)
    {
        protocol_coordinator_tick();
        sleep_ms(1u);
    }
    assert(atomic_load(&runtime.airplay_restart_started));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 2u,
                                            &second));
    protocol_coordinator_media_end(&second);
    assert(protocol_coordinator_media_drive_resources(&second.lease, 1000u));
    assert(atomic_load(&runtime.airplay_cancelled));
    assert(snapshot().services[PROTOCOL_SERVICE_AIRPLAY] ==
           PROTOCOL_SERVICE_STOPPED);
    assert(protocol_coordinator_media_release(&second.lease));
    atomic_store(&runtime.airplay_cancelled, true);
    protocol_coordinator_stop();
}

static void test_stalled_service_does_not_block_start(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, false}
    };
    DelayedRuntime runtime;
    ProtocolCoordinatorOperations operations = {
        .context = &runtime,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = delayed_iptv_start,
                .stop = delayed_stop,
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = delayed_dlna_start,
                .stop = delayed_stop,
            },
        },
    };
    uint64_t start_ms;
    uint64_t elapsed_ms;
    unsigned int attempt;

    atomic_init(&runtime.iptv_started, false);
    atomic_init(&runtime.iptv_finished, false);
    atomic_init(&runtime.dlna_started, false);
    atomic_init(&runtime.stop_count, 0);
    protocol_coordinator_reset();
    start_ms = monotonic_ms();
    assert(protocol_coordinator_start(&config, &operations));
    elapsed_ms = monotonic_ms() - start_ms;
    assert(elapsed_ms < 50u);

    for (attempt = 0; attempt < 100u &&
                      !atomic_load(&runtime.dlna_started); ++attempt)
        sleep_ms(1);
    assert(atomic_load(&runtime.iptv_started));
    assert(atomic_load(&runtime.dlna_started));
    assert(!atomic_load(&runtime.iptv_finished));
    assert(protocol_coordinator_get_snapshot(&(ProtocolCoordinatorSnapshot){0}));
    protocol_coordinator_stop();
    assert(atomic_load(&runtime.iptv_finished));
    assert(atomic_load(&runtime.stop_count) == 2);
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPED);
    protocol_coordinator_stop();
    assert(atomic_load(&runtime.stop_count) == 2);
}

static void test_serial_startup_preserves_order_without_blocking_start(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, false},
        .serial_startup = true,
    };
    DelayedRuntime runtime;
    ProtocolCoordinatorOperations operations = {
        .context = &runtime,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = delayed_iptv_start,
                .stop = delayed_stop,
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = delayed_dlna_start,
                .stop = delayed_stop,
            },
        },
    };
    uint64_t start_ms;
    uint64_t elapsed_ms;
    unsigned int attempt;

    atomic_init(&runtime.iptv_started, false);
    atomic_init(&runtime.iptv_finished, false);
    atomic_init(&runtime.dlna_started, false);
    atomic_init(&runtime.stop_count, 0);
    protocol_coordinator_reset();
    start_ms = monotonic_ms();
    assert(protocol_coordinator_start(&config, &operations));
    elapsed_ms = monotonic_ms() - start_ms;
    assert(elapsed_ms < 50u);

    for (attempt = 0; attempt < 100u &&
                      !atomic_load(&runtime.iptv_started); ++attempt)
        sleep_ms(1u);
    assert(atomic_load(&runtime.iptv_started));
    assert(!atomic_load(&runtime.dlna_started));

    for (attempt = 0; attempt < 1000u &&
                      !atomic_load(&runtime.iptv_finished); ++attempt)
        sleep_ms(1u);
    assert(atomic_load(&runtime.iptv_finished));
    assert(!atomic_load(&runtime.dlna_started));

    for (attempt = 0; attempt < 1000u &&
                      !atomic_load(&runtime.dlna_started); ++attempt)
    {
        protocol_coordinator_tick();
        sleep_ms(1u);
    }
    assert(atomic_load(&runtime.dlna_started));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));
    protocol_coordinator_stop();
    assert(atomic_load(&runtime.stop_count) == 2);
}

static void fake_event(FakeRuntime *runtime, char event)
{
    pthread_mutex_lock(&runtime->mutex);
    assert(runtime->event_count + 1u < sizeof(runtime->events));
    runtime->events[runtime->event_count++] = event;
    runtime->events[runtime->event_count] = '\0';
    pthread_mutex_unlock(&runtime->mutex);
}

static void fake_runtime_init(FakeRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    assert(pthread_mutex_init(&runtime->mutex, NULL) == 0);
}

static void fake_runtime_destroy(FakeRuntime *runtime)
{
    assert(pthread_mutex_destroy(&runtime->mutex) == 0);
}

static int fake_event_count(FakeRuntime *runtime, char event)
{
    size_t index;
    int count = 0;

    pthread_mutex_lock(&runtime->mutex);
    for (index = 0; index < runtime->event_count; ++index)
    {
        if (runtime->events[index] == event)
            ++count;
    }
    pthread_mutex_unlock(&runtime->mutex);
    return count;
}

static bool wait_for_event_count(FakeRuntime *runtime, char event,
                                 int expected)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000u; ++attempt)
    {
        if (fake_event_count(runtime, event) >= expected)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool wait_for_service_state(ProtocolService service,
                                   ProtocolServiceState expected)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000u; ++attempt)
    {
        if (snapshot().services[service] == expected)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool resource_immediate_start(void *context)
{
    (void)context;
    return true;
}

static bool resource_cancellable_airplay_start(void *context)
{
    ResourceStopRuntime *runtime = context;
    int count = atomic_fetch_add(&runtime->airplay_start_count, 1) + 1;

    if (count == 1)
        return true;
    atomic_store(&runtime->airplay_cancelled, false);
    atomic_store(&runtime->airplay_restart_started, true);
    while (!atomic_load(&runtime->airplay_cancelled))
        sleep_ms(1u);
    return true;
}

static void resource_request_stop(void *context)
{
    ResourceStopRuntime *runtime = context;

    atomic_store(&runtime->airplay_cancelled, true);
}

static void resource_stop(void *context)
{
    ResourceStopRuntime *runtime = context;

    atomic_fetch_add(&runtime->stop_count, 1);
}

static bool resource_background_suspend(void *context, bool suspended)
{
    ResourceStopRuntime *runtime = context;

    if (suspended)
    {
        atomic_fetch_add(&runtime->background_calls, 1);
        if (atomic_exchange(&runtime->background_fail_once, false))
            return false;
    }
    return true;
}

static bool wait_for_resource_mode(ProtocolResourceMode expected)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000u; ++attempt)
    {
        ProtocolCoordinatorSnapshot value;

        protocol_coordinator_tick();
        value = snapshot();
        if (!value.resource_transition_active &&
            value.applied_resource_mode == expected)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool wait_for_resource_failure(void)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 2000u; ++attempt)
    {
        ProtocolCoordinatorSnapshot value;

        protocol_coordinator_tick();
        value = snapshot();
        if (!value.resource_transition_active &&
            value.resource_transition_failed)
            return true;
        sleep_ms(1);
    }
    return false;
}

static bool fake_iptv_start(void *context)
{
    FakeRuntime *runtime = context;
    bool result;

    fake_event(runtime, 'i');
    pthread_mutex_lock(&runtime->mutex);
    result = runtime->iptv_result;
    pthread_mutex_unlock(&runtime->mutex);
    return result;
}

static void fake_iptv_stop(void *context)
{
    fake_event(context, 'I');
}

static bool fake_dlna_start(void *context)
{
    FakeRuntime *runtime = context;
    bool result;

    fake_event(runtime, 'd');
    pthread_mutex_lock(&runtime->mutex);
    result = runtime->dlna_result;
    pthread_mutex_unlock(&runtime->mutex);
    return result;
}

static void fake_dlna_stop(void *context)
{
    fake_event(context, 'D');
}

static bool fake_airplay_start(void *context)
{
    FakeRuntime *runtime = context;
    bool result;

    fake_event(runtime, 'a');
    pthread_mutex_lock(&runtime->mutex);
    result = runtime->airplay_result;
    pthread_mutex_unlock(&runtime->mutex);
    return result;
}

static void fake_airplay_stop(void *context)
{
    fake_event(context, 'A');
}

static bool fake_airplay_get_status(void *context,
                                    ProtocolAirPlayStatus *status_out)
{
    FakeRuntime *runtime = context;

    assert(status_out != NULL);
    pthread_mutex_lock(&runtime->mutex);
    *status_out = runtime->airplay;
    pthread_mutex_unlock(&runtime->mutex);
    return true;
}

static void fake_set_discovery_suspended(void *context, bool suspended)
{
    FakeRuntime *runtime = context;
    ProtocolCoordinatorSnapshot value;

    /* The callback contract requires coordinator locks to be released. */
    assert(protocol_coordinator_get_snapshot(&value));
    assert(value.discovery_suspended == suspended);
    fake_event(runtime, suspended ? 's' : 'r');
}

static bool fake_set_background_network_suspended(void *context,
                                                   bool suspended)
{
    FakeRuntime *runtime = context;
    ProtocolCoordinatorSnapshot value;
    bool result = true;

    /* Resource callbacks must also run without coordinator locks held. */
    assert(protocol_coordinator_get_snapshot(&value));
    fake_event(runtime, suspended ? 'b' : 'g');
    if (suspended)
    {
        pthread_mutex_lock(&runtime->mutex);
        runtime->background_calls++;
        if (runtime->background_fail_once)
        {
            runtime->background_fail_once = false;
            result = false;
        }
        pthread_mutex_unlock(&runtime->mutex);
    }
    return result;
}

static ProtocolCoordinatorOperations fake_operations(FakeRuntime *runtime)
{
    ProtocolCoordinatorOperations operations = {
        .context = runtime,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = fake_iptv_start,
                .stop = fake_iptv_stop,
                .stop_after_start_attempt = true
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = fake_dlna_start,
                .stop = fake_dlna_stop
            },
            [PROTOCOL_SERVICE_AIRPLAY] = {
                .start = fake_airplay_start,
                .stop = fake_airplay_stop
            }
        },
        .airplay_get_status = fake_airplay_get_status,
        .set_discovery_suspended = fake_set_discovery_suspended,
        .set_background_network_suspended =
            fake_set_background_network_suspended,
    };

    return operations;
}

static ProtocolCoordinatorSnapshot snapshot(void)
{
    ProtocolCoordinatorSnapshot value;

    assert(protocol_coordinator_get_snapshot(&value));
    return value;
}

static void start_ready(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true}
    };

    protocol_coordinator_reset();
    assert(protocol_coordinator_begin_start(&config));
    assert(protocol_coordinator_set_service_state(PROTOCOL_SERVICE_IPTV,
                                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_set_service_state(PROTOCOL_SERVICE_DLNA,
                                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_set_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                                  PROTOCOL_SERVICE_RUNNING));
}

static void test_lifecycle_and_degraded_start(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, false}
    };
    ProtocolCoordinatorSnapshot value;

    protocol_coordinator_reset();
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_STOPPED);
    assert(protocol_coordinator_begin_start(&config));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_STARTING);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_DISABLED);

    assert(protocol_coordinator_set_service_state(PROTOCOL_SERVICE_IPTV,
                                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_set_service_state(PROTOCOL_SERVICE_DLNA,
                                                  PROTOCOL_SERVICE_FAILED));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_READY);
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_FAILED);
    assert(!protocol_coordinator_begin_start(&config));

    assert(protocol_coordinator_begin_stop());
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPING);
    assert(!protocol_coordinator_begin_stop());
    protocol_coordinator_finish_stop();
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_STOPPED);
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_STOPPED);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_STOPPED);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_DISABLED);
}

static void test_runtime_operation_order(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true}
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolCoordinatorSnapshot value;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    snprintf(runtime.airplay.status, sizeof(runtime.airplay.status),
             "Ready for AirPlay video");
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_event_count(&runtime, 'i', 1));
    assert(wait_for_event_count(&runtime, 'd', 1));
    assert(wait_for_event_count(&runtime, 'a', 1));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_READY);
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_RUNNING);

    pthread_mutex_lock(&runtime.mutex);
    runtime.airplay.pin_visible = true;
    memcpy(runtime.airplay.pin, "1234", sizeof(runtime.airplay.pin));
    snprintf(runtime.airplay.status, sizeof(runtime.airplay.status),
             "Ready for AirPlay video");
    pthread_mutex_unlock(&runtime.mutex);
    protocol_coordinator_tick();
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_READY);
    assert(value.airplay.running);
    assert(value.airplay.pin_visible);
    assert(strcmp(value.airplay.pin, "1234") == 0);

    protocol_coordinator_stop();
    assert(fake_event_count(&runtime, 'A') == 1);
    assert(fake_event_count(&runtime, 'D') == 1);
    assert(fake_event_count(&runtime, 'I') == 1);
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPED);
    protocol_coordinator_stop();
    assert(fake_event_count(&runtime, 'A') == 1);
    assert(fake_event_count(&runtime, 'D') == 1);
    assert(fake_event_count(&runtime, 'I') == 1);
    fake_runtime_destroy(&runtime);
}

static void test_runtime_degraded_and_network_disabled(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true}
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolCoordinatorSnapshot value;

    fake_runtime_init(&runtime);
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_FAILED));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_FAILED));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_FAILED));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_FAILED);
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_FAILED);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_FAILED);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_FAILED);
    protocol_coordinator_stop();
    assert(fake_event_count(&runtime, 'I') == 1);
    assert(fake_event_count(&runtime, 'D') == 0);
    assert(fake_event_count(&runtime, 'A') == 0);
    fake_runtime_destroy(&runtime);

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    config.enabled[PROTOCOL_SERVICE_DLNA] = false;
    config.enabled[PROTOCOL_SERVICE_AIRPLAY] = false;
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_RUNNING));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_READY);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_DISABLED);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_DISABLED);
    protocol_coordinator_stop();
    assert(fake_event_count(&runtime, 'i') == 1);
    assert(fake_event_count(&runtime, 'I') == 1);
    fake_runtime_destroy(&runtime);
}

static void test_media_takeover_and_stale_lease(void)
{
    ProtocolMediaTransaction dlna;
    ProtocolMediaTransaction airplay;
    ProtocolCoordinatorSnapshot value;

    start_ready();
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u, &dlna));
    assert(dlna.previous.owner == PLAYER_MEDIA_OWNER_NONE);
    assert(protocol_coordinator_media_validate(&dlna.lease));
    assert(snapshot().state == PROTOCOL_COORDINATOR_MEDIA_ACTIVE);
    protocol_coordinator_media_end(&dlna);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                            44u, &airplay));
    assert(airplay.previous.owner == PLAYER_MEDIA_OWNER_DLNA);
    assert(!protocol_coordinator_media_validate(&dlna.lease));
    assert(protocol_coordinator_media_validate(&airplay.lease));
    protocol_coordinator_media_end(&airplay);

    assert(!protocol_coordinator_media_release(&dlna.lease));
    assert(protocol_coordinator_media_release(&airplay.lease));
    value = snapshot();
    assert(value.state == PROTOCOL_COORDINATOR_READY);
    assert(value.active_media.owner == PLAYER_MEDIA_OWNER_NONE);

    assert(protocol_coordinator_begin_stop());
    assert(!protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_IPTV, 1u, &dlna));
    protocol_coordinator_finish_stop();
}

static void test_media_guard(void)
{
    ProtocolMediaTransaction iptv;
    ProtocolMediaTransaction dlna;
    ProtocolMediaGuard guard;

    start_ready();
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_IPTV, 1u, &iptv));
    protocol_coordinator_media_end(&iptv);
    assert(protocol_coordinator_media_guard_begin(PLAYER_MEDIA_OWNER_IPTV, 1u,
                                                  &guard));
    assert(protocol_coordinator_media_validate(&guard.lease));
    protocol_coordinator_media_guard_end(&guard);
    assert(!protocol_coordinator_media_guard_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                                   &guard));
    assert(protocol_coordinator_media_release(&iptv.lease));
    assert(protocol_coordinator_media_unowned_or_owner_guard_begin(
        PLAYER_MEDIA_OWNER_DLNA, 1u, &guard));
    protocol_coordinator_media_guard_end(&guard);
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u, &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(protocol_coordinator_media_lease_guard_begin(&dlna.lease, false,
                                                        &guard));
    protocol_coordinator_media_guard_end(&guard);
    assert(!protocol_coordinator_media_lease_guard_begin(&iptv.lease, false,
                                                         &guard));
    assert(protocol_coordinator_begin_stop());
    assert(!protocol_coordinator_media_lease_guard_begin(&dlna.lease, false,
                                                         &guard));
    assert(protocol_coordinator_media_lease_guard_begin(&dlna.lease, true,
                                                        &guard));
    protocol_coordinator_media_guard_end(&guard);
    assert(protocol_coordinator_media_barrier_begin(&guard));
    protocol_coordinator_media_guard_end(&guard);
    protocol_coordinator_finish_stop();
}

static void test_media_abort(void)
{
    ProtocolMediaTransaction transaction;

    start_ready();
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &transaction));
    protocol_coordinator_media_abort(&transaction);
    assert(!transaction.active);
    assert(!protocol_coordinator_media_validate(&transaction.lease));
    assert(snapshot().state == PROTOCOL_COORDINATOR_READY);
    assert(protocol_coordinator_begin_stop());
    protocol_coordinator_finish_stop();
}

static void test_exclusive_resource_modes_and_first_owner(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolMediaTransaction replacement;
    ProtocolMediaTransaction airplay;
    ProtocolMediaTransaction iptv;
    ProtocolCoordinatorSnapshot value;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);

    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    value = snapshot();
    assert(value.desired_resource_mode == PROTOCOL_RESOURCE_MODE_HOME);
    assert(value.applied_resource_mode == PROTOCOL_RESOURCE_MODE_HOME);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(!protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                             44u, &airplay));
    assert(protocol_coordinator_media_validate(&dlna.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE));
    value = snapshot();
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_STOPPED);
    assert(fake_event_count(&runtime, 'A') == 1);
    assert(fake_event_count(&runtime, 'b') == 1);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 2u,
                                            &replacement));
    assert(replacement.previous.owner == PLAYER_MEDIA_OWNER_DLNA);
    protocol_coordinator_media_end(&replacement);
    assert(!protocol_coordinator_media_validate(&dlna.lease));
    assert(protocol_coordinator_media_validate(&replacement.lease));
    assert(fake_event_count(&runtime, 'A') == 1);

    assert(protocol_coordinator_media_release(&replacement.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(fake_event_count(&runtime, 'a') == 2);
    assert(fake_event_count(&runtime, 'g') == 1);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR,
                                            55u, &airplay));
    protocol_coordinator_media_end(&airplay);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE));
    value = snapshot();
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_STOPPED);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_RUNNING);
    assert(protocol_coordinator_media_release(&airplay.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_IPTV, 77u,
                                            &iptv));
    protocol_coordinator_media_end(&iptv);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_IPTV_EXCLUSIVE));
    value = snapshot();
    assert(value.services[PROTOCOL_SERVICE_IPTV] == PROTOCOL_SERVICE_RUNNING);
    assert(value.services[PROTOCOL_SERVICE_DLNA] == PROTOCOL_SERVICE_STOPPED);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_STOPPED);
    assert(protocol_coordinator_media_release(&iptv.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));

    protocol_coordinator_stop();
    assert(snapshot().state == PROTOCOL_COORDINATOR_STOPPED);
    fake_runtime_destroy(&runtime);
}

static void test_exclusive_resource_restart_retries(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolCoordinatorSnapshot value;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE));

    pthread_mutex_lock(&runtime.mutex);
    runtime.airplay_result = false;
    pthread_mutex_unlock(&runtime.mutex);
    assert(protocol_coordinator_media_release(&dlna.lease));
    assert(wait_for_resource_failure());
    value = snapshot();
    assert(value.desired_resource_mode == PROTOCOL_RESOURCE_MODE_HOME);
    assert(value.applied_resource_mode ==
           PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE);
    assert(value.services[PROTOCOL_SERVICE_AIRPLAY] == PROTOCOL_SERVICE_FAILED);

    pthread_mutex_lock(&runtime.mutex);
    runtime.airplay_result = true;
    pthread_mutex_unlock(&runtime.mutex);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(fake_event_count(&runtime, 'a') >= 3);
    protocol_coordinator_stop();
    fake_runtime_destroy(&runtime);
}

static void test_background_quiesce_failure_retries(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolCoordinatorSnapshot value;
    bool converged = false;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    runtime.background_fail_once = true;
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 9u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(wait_for_resource_failure());
    value = snapshot();
    assert(value.desired_resource_mode ==
           PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE);
    assert(value.applied_resource_mode == PROTOCOL_RESOURCE_MODE_HOME);

    for (unsigned attempt = 0u; attempt < 2500u; ++attempt)
    {
        protocol_coordinator_tick();
        value = snapshot();
        if (!value.resource_transition_active &&
            value.applied_resource_mode ==
                PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE)
        {
            converged = true;
            break;
        }
        sleep_ms(1u);
    }
    assert(converged);
    pthread_mutex_lock(&runtime.mutex);
    assert(runtime.background_calls >= 2u);
    pthread_mutex_unlock(&runtime.mutex);
    assert(protocol_coordinator_media_release(&dlna.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    protocol_coordinator_stop();
    fake_runtime_destroy(&runtime);
}

static void test_terminal_playback_releases_exclusive_owner(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolMediaTransaction airplay;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    protocol_coordinator_observe_playback(false, true);
    assert(protocol_coordinator_media_validate(&dlna.lease));
    protocol_coordinator_observe_playback(true, false);
    protocol_coordinator_observe_playback(true, false);
    assert(protocol_coordinator_media_validate(&dlna.lease));
    protocol_coordinator_observe_playback(false, true);
    assert(!protocol_coordinator_media_validate(&dlna.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                            44u, &airplay));
    protocol_coordinator_media_end(&airplay);
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE));
    protocol_coordinator_observe_playback(false, true);
    assert(protocol_coordinator_media_validate(&airplay.lease));
    sleep_ms(1001u);
    protocol_coordinator_observe_playback(false, true);
    assert(!protocol_coordinator_media_validate(&airplay.lease));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    protocol_coordinator_stop();
    fake_runtime_destroy(&runtime);
}

static void test_terminal_observation_preserves_legacy_owner(void)
{
    ProtocolMediaTransaction dlna;

    start_ready();
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    protocol_coordinator_observe_playback(true, false);
    protocol_coordinator_observe_playback(false, true);
    assert(protocol_coordinator_media_validate(&dlna.lease));
    assert(protocol_coordinator_media_release(&dlna.lease));
    assert(protocol_coordinator_begin_stop());
    protocol_coordinator_finish_stop();
}

static void test_stop_unblocks_resource_waiter(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    ProtocolMediaTransaction dlna;
    ResourceWaitThread waiter;
    pthread_t thread;

    memset(&waiter, 0, sizeof(waiter));
    atomic_init(&waiter.entered, false);
    protocol_coordinator_reset();
    assert(protocol_coordinator_begin_start(&config));
    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    waiter.lease = dlna.lease;
    assert(pthread_create(&thread, NULL, resource_wait_thread_main,
                          &waiter) == 0);
    while (!atomic_load(&waiter.entered))
        sleep_ms(1u);
    sleep_ms(2u);
    assert(protocol_coordinator_begin_stop());
    assert(pthread_join(thread, NULL) == 0);
    assert(!waiter.result);
    protocol_coordinator_finish_stop();
}

static void test_discovery_suspension_follows_media_ownership(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .discovery_suspension_policy =
            PROTOCOL_DISCOVERY_SUSPEND_MEDIA_OWNERSHIP
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolMediaTransaction airplay;
    ProtocolMediaTransaction iptv;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);

    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(!snapshot().discovery_suspended);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    assert(snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 's') == 1);
    protocol_coordinator_media_end(&dlna);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                            44u, &airplay));
    assert(snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 's') == 1);
    protocol_coordinator_media_end(&airplay);

    assert(!protocol_coordinator_media_release(&dlna.lease));
    assert(snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 0);
    assert(protocol_coordinator_media_release(&airplay.lease));
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 1);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_IPTV, 2u,
                                            &iptv));
    assert(fake_event_count(&runtime, 's') == 2);
    protocol_coordinator_media_abort(&iptv);
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 2);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 3u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(fake_event_count(&runtime, 's') == 3);
    player_ownership_reset();
    protocol_coordinator_tick();
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 3);

    protocol_coordinator_stop();
    assert(fake_event_count(&runtime, 'r') == 3);
    fake_runtime_destroy(&runtime);
}

static void test_discovery_suspension_follows_playback_activity(void)
{
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .discovery_suspension_policy =
            PROTOCOL_DISCOVERY_SUSPEND_PLAYBACK_ACTIVITY
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    ProtocolMediaTransaction dlna;
    ProtocolMediaTransaction airplay;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);

    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_IPTV,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_DLNA,
                                  PROTOCOL_SERVICE_RUNNING));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA, 1u,
                                            &dlna));
    protocol_coordinator_media_end(&dlna);
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 's') == 0);

    protocol_coordinator_set_playback_active(true);
    assert(snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 's') == 1);
    protocol_coordinator_set_playback_active(true);
    assert(fake_event_count(&runtime, 's') == 1);

    assert(protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO,
                                            44u, &airplay));
    protocol_coordinator_media_end(&airplay);
    assert(snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 's') == 1);

    protocol_coordinator_set_playback_active(false);
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 1);
    protocol_coordinator_set_playback_active(false);
    assert(fake_event_count(&runtime, 'r') == 1);

    protocol_coordinator_set_playback_active(true);
    assert(fake_event_count(&runtime, 's') == 2);
    protocol_coordinator_stop();
    assert(!snapshot().discovery_suspended);
    assert(fake_event_count(&runtime, 'r') == 2);
    fake_runtime_destroy(&runtime);
}

static void *claim_thread_main(void *argument)
{
    ClaimThread *claim = argument;
    ProtocolMediaTransaction transaction;

    claim->acquired = protocol_coordinator_media_begin(claim->owner, claim->token,
                                                       &transaction);
    if (claim->acquired)
        protocol_coordinator_media_end(&transaction);
    return NULL;
}

static void test_concurrent_takeover(void)
{
    enum { THREAD_COUNT = 12 };
    pthread_t threads[THREAD_COUNT];
    ClaimThread claims[THREAD_COUNT];
    PlayerOwnershipLease current;
    int index;

    start_ready();
    for (index = 0; index < THREAD_COUNT; ++index)
    {
        claims[index].owner = (index % 2 == 0) ? PLAYER_MEDIA_OWNER_DLNA
                                               : PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO;
        claims[index].token = (uint64_t)index + 1u;
        claims[index].acquired = false;
        assert(pthread_create(&threads[index], NULL, claim_thread_main,
                              &claims[index]) == 0);
    }
    for (index = 0; index < THREAD_COUNT; ++index)
    {
        assert(pthread_join(threads[index], NULL) == 0);
        assert(claims[index].acquired);
    }

    assert(protocol_coordinator_media_current(&current));
    assert(current.owner == PLAYER_MEDIA_OWNER_DLNA ||
           current.owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO);
    assert(current.generation != 0u);
    assert(protocol_coordinator_media_release(&current));
    assert(protocol_coordinator_begin_stop());
    protocol_coordinator_finish_stop();
}

static void test_concurrent_exclusive_claims_choose_one_family(void)
{
    enum { THREAD_COUNT = 12 };
    ProtocolCoordinatorConfig config = {
        .enabled = {true, true, true},
        .exclusive_media_resources = true,
    };
    FakeRuntime runtime;
    ProtocolCoordinatorOperations operations;
    pthread_t threads[THREAD_COUNT];
    ClaimThread claims[THREAD_COUNT];
    PlayerOwnershipLease current;
    ProtocolResourceMode winning_mode;
    int acquired_dlna = 0;
    int acquired_airplay = 0;
    int index;

    fake_runtime_init(&runtime);
    runtime.iptv_result = true;
    runtime.dlna_result = true;
    runtime.airplay_result = true;
    runtime.airplay.running = true;
    operations = fake_operations(&runtime);
    protocol_coordinator_reset();
    assert(protocol_coordinator_start(&config, &operations));
    assert(wait_for_service_state(PROTOCOL_SERVICE_AIRPLAY,
                                  PROTOCOL_SERVICE_RUNNING));

    for (index = 0; index < THREAD_COUNT; ++index)
    {
        claims[index].owner = (index % 2 == 0) ? PLAYER_MEDIA_OWNER_DLNA
                                               : PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO;
        claims[index].token = (uint64_t)index + 1u;
        claims[index].acquired = false;
        assert(pthread_create(&threads[index], NULL, claim_thread_main,
                              &claims[index]) == 0);
    }
    for (index = 0; index < THREAD_COUNT; ++index)
    {
        assert(pthread_join(threads[index], NULL) == 0);
        if (claims[index].acquired &&
            claims[index].owner == PLAYER_MEDIA_OWNER_DLNA)
            ++acquired_dlna;
        if (claims[index].acquired &&
            claims[index].owner == PLAYER_MEDIA_OWNER_AIRPLAY_VIDEO)
            ++acquired_airplay;
    }
    assert((acquired_dlna > 0) != (acquired_airplay > 0));
    assert(protocol_coordinator_media_current(&current));
    winning_mode = current.owner == PLAYER_MEDIA_OWNER_DLNA
                       ? PROTOCOL_RESOURCE_MODE_DLNA_EXCLUSIVE
                       : PROTOCOL_RESOURCE_MODE_AIRPLAY_EXCLUSIVE;
    assert(snapshot().desired_resource_mode == winning_mode);
    assert(protocol_coordinator_media_release(&current));
    assert(wait_for_resource_mode(PROTOCOL_RESOURCE_MODE_HOME));
    protocol_coordinator_stop();
    fake_runtime_destroy(&runtime);
}

int main(void)
{
    test_stalled_service_does_not_block_start();
    test_serial_startup_preserves_order_without_blocking_start();
    test_stop_cancels_start_worker_before_join();
    test_stop_cancels_resource_transition_before_join();
    test_reclaim_cancels_superseded_home_restart();
    test_lifecycle_and_degraded_start();
    test_runtime_operation_order();
    test_runtime_degraded_and_network_disabled();
    test_media_takeover_and_stale_lease();
    test_exclusive_resource_modes_and_first_owner();
    test_exclusive_resource_restart_retries();
    test_background_quiesce_failure_retries();
    test_terminal_playback_releases_exclusive_owner();
    test_terminal_observation_preserves_legacy_owner();
    test_stop_unblocks_resource_waiter();
    test_media_guard();
    test_media_abort();
    test_discovery_suspension_follows_media_ownership();
    test_discovery_suspension_follows_playback_activity();
    test_concurrent_takeover();
    test_concurrent_exclusive_claims_choose_one_family();
    puts("protocol coordinator tests passed");
    return 0;
}
