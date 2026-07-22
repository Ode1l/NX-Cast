#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "app/network_diagnostics.h"

enum
{
    TEST_THREAD_COUNT = 4,
    TEST_THREAD_ITERATIONS = 1000
};

static void test_sleep_ms(unsigned milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };

    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

static void *test_operation_worker(void *opaque)
{
    NetworkDiagnosticSubsystem subsystem =
        (NetworkDiagnosticSubsystem)(uintptr_t)opaque;

    for (unsigned index = 0u; index < TEST_THREAD_ITERATIONS; ++index)
    {
        NetworkOperationToken token = network_diagnostics_operation_begin(
            subsystem, NETWORK_OPERATION_RECV);
        assert(token.active);
        network_diagnostics_operation_end(&token, 0);
        assert(!token.active);
    }
    return NULL;
}

static void test_socket_balance_and_reset(void)
{
    NetworkDiagnosticSnapshot snapshot;

    assert(network_diagnostics_reset());
    network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_MDNS);
    network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_MDNS);
    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_MDNS);
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS,
                                            &snapshot));
    assert(snapshot.open_sockets == 1u);
    assert(snapshot.sockets_opened == 2u);
    assert(snapshot.sockets_closed == 1u);
    assert(!network_diagnostics_reset());
    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_MDNS);
    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_MDNS);
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS,
                                            &snapshot));
    assert(snapshot.open_sockets == 0u);
    assert(snapshot.sockets_closed == 2u);
    assert(snapshot.socket_close_underflows == 1u);
    assert(network_diagnostics_reset());
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS,
                                            &snapshot));
    assert(snapshot.sockets_opened == 0u);
    assert(snapshot.socket_close_underflows == 0u);
}

static void test_nested_operations_and_error(void)
{
    NetworkDiagnosticSnapshot snapshot;
    NetworkOperationToken first;
    NetworkOperationToken second;

    assert(network_diagnostics_reset());
    first = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, NETWORK_OPERATION_SELECT);
    /* Cross at least one Windows/Cygwin monotonic-clock tick. */
    test_sleep_ms(25u);
    second = network_diagnostics_operation_begin(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, NETWORK_OPERATION_ACCEPT);
    assert(network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, &snapshot));
    assert(snapshot.active_operations == 2u);
    assert(snapshot.oldest_active_token == first.token);
    assert(snapshot.oldest_active_operation == NETWORK_OPERATION_SELECT);
    assert(snapshot.oldest_active_age_ms >= 10u);
    network_diagnostics_operation_end(&second, ECONNRESET);
    network_diagnostics_operation_end(&first, 0);
    network_diagnostics_operation_end(&first, EINVAL);
    assert(network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL, &snapshot));
    assert(snapshot.active_operations == 0u);
    assert(snapshot.operation_count == 2u);
    assert(snapshot.maximum_duration_ms >= snapshot.last_duration_ms);
    assert(snapshot.maximum_duration_ms >= 10u);
    assert(snapshot.last_error == ECONNRESET);
    assert(snapshot.last_error_operation == NETWORK_OPERATION_ACCEPT);
}

static void test_bounded_active_slots(void)
{
    NetworkOperationToken tokens[NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT + 2u];
    NetworkDiagnosticSnapshot snapshot;

    assert(network_diagnostics_reset());
    for (size_t index = 0u;
         index < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT + 2u; ++index)
        tokens[index] = network_diagnostics_operation_begin(
            NETWORK_DIAGNOSTIC_SSDP, NETWORK_OPERATION_SEND);
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_SSDP,
                                            &snapshot));
    assert(snapshot.active_operations ==
           NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT + 2u);
    assert(snapshot.operation_slot_overflows == 2u);
    for (size_t index = 0u;
         index < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT + 2u; ++index)
        network_diagnostics_operation_end(&tokens[index], 0);
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_SSDP,
                                            &snapshot));
    assert(snapshot.active_operations == 0u);
}

static void test_concurrent_operations(void)
{
    pthread_t threads[TEST_THREAD_COUNT];
    NetworkDiagnosticSnapshot snapshot;

    assert(network_diagnostics_reset());
    for (size_t index = 0u; index < TEST_THREAD_COUNT; ++index)
        assert(pthread_create(&threads[index], NULL, test_operation_worker,
                              (void *)(uintptr_t)NETWORK_DIAGNOSTIC_DLNA_HTTP) ==
               0);
    for (size_t index = 0u; index < TEST_THREAD_COUNT; ++index)
        assert(pthread_join(threads[index], NULL) == 0);
    assert(network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_DLNA_HTTP,
                                            &snapshot));
    assert(snapshot.active_operations == 0u);
    assert(snapshot.operation_count ==
           (uint64_t)TEST_THREAD_COUNT * TEST_THREAD_ITERATIONS);
    assert(snapshot.operation_slot_overflows == 0u);
}

static void test_names_and_invalid_inputs(void)
{
    NetworkDiagnosticSnapshot snapshot;

    assert(!network_diagnostics_get_snapshot(
        NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT, &snapshot));
    assert(!network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS, NULL));
    assert(network_diagnostics_operation_name(NETWORK_OPERATION_RECV)[0] ==
           'r');
    assert(network_diagnostics_subsystem_name(NETWORK_DIAGNOSTIC_MDNS)[0] ==
           'm');
}

int main(void)
{
    test_socket_balance_and_reset();
    test_nested_operations_and_error();
    test_bounded_active_slots();
    test_concurrent_operations();
    test_names_and_invalid_inputs();
    puts("network diagnostics tests passed");
    return 0;
}
