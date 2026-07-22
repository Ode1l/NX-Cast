#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/network_diagnostics.h"
#include "app/runtime_diagnostics.h"

static void test_thread_lifecycle(void)
{
    RuntimeDiagnosticThreadSnapshot snapshot;
    uint32_t first;
    uint32_t second;

    assert(runtime_diagnostics_reset());
    first = runtime_diagnostics_thread_created(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT);
    second = runtime_diagnostics_thread_created(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT);
    assert(first == 1u);
    assert(second == 2u);
    runtime_diagnostics_thread_create_failed(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT);
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, &snapshot));
    assert(snapshot.created == 2u);
    assert(snapshot.joined == 0u);
    assert(snapshot.live == 2u);
    assert(snapshot.create_failures == 1u);
    assert(snapshot.generation == 2u);
    assert(runtime_diagnostics_thread_joined(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, first));
    assert(runtime_diagnostics_thread_joined(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, second));
    assert(!runtime_diagnostics_thread_joined(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, second));
    assert(runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT, &snapshot));
    assert(snapshot.joined == 2u);
    assert(snapshot.live == 0u);
    assert(snapshot.join_underflows == 1u);
    assert(runtime_diagnostics_reset());
}

static void test_resource_snapshot_and_format(void)
{
    RuntimeDiagnosticResourceSnapshot snapshot;
    char text[1536];

    assert(runtime_diagnostics_reset());
    assert(network_diagnostics_reset());
    runtime_diagnostics_thread_created(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS);
    network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_MDNS);
    network_diagnostics_socket_opened(NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO);
    assert(runtime_diagnostics_collect_resources(&snapshot));
    assert(snapshot.app_threads_live == 1u);
    assert(snapshot.app_threads_created == 1u);
    assert(snapshot.open_sockets == 2u);
    assert(snapshot.open_sockets_by_subsystem[NETWORK_DIAGNOSTIC_MDNS] == 1u);
    assert(snapshot.open_sockets_by_subsystem[
               NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO] == 1u);
    assert(runtime_diagnostics_format_resource_snapshot(
        &snapshot, "claim", "dlna", 17u, text, sizeof(text)));
    assert(strstr(text, "event=claim") != NULL);
    assert(strstr(text, "owner=dlna") != NULL);
    assert(strstr(text, "generation=17") != NULL);
    assert(strstr(text, "app_threads=1/1/0/0/0") != NULL);
    assert(strstr(text, "sockets=2") != NULL);
    assert(strstr(text, "airplay-audio=1") != NULL);

    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_MDNS);
    network_diagnostics_socket_closed(NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO);
    assert(runtime_diagnostics_thread_joined(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS, 1u));
    assert(runtime_diagnostics_reset());
    assert(network_diagnostics_reset());
}

static void test_names_and_invalid_inputs(void)
{
    RuntimeDiagnosticThreadSnapshot snapshot;

    assert(strcmp(runtime_diagnostics_thread_name(
                      RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME),
                  "airplay-mirror-runtime") == 0);
    assert(strcmp(runtime_diagnostics_thread_name(
                      RUNTIME_DIAGNOSTIC_THREAD_COUNT),
                  "unknown") == 0);
    assert(!runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_COUNT, &snapshot));
    assert(!runtime_diagnostics_get_thread_snapshot(
        RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS, NULL));
    assert(!runtime_diagnostics_collect_resources(NULL));
}

int main(void)
{
    test_thread_lifecycle();
    test_resource_snapshot_and_format();
    test_names_and_invalid_inputs();
    puts("runtime diagnostics tests passed");
    return 0;
}
