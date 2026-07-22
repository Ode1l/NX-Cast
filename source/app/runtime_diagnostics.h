#ifndef NXCAST_APP_RUNTIME_DIAGNOSTICS_H
#define NXCAST_APP_RUNTIME_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "network_diagnostics.h"

typedef enum
{
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS = 0,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_LISTENER,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_TIMING,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_AUDIO,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR,
    RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME,
    RUNTIME_DIAGNOSTIC_THREAD_COUNT
} RuntimeDiagnosticThreadRole;

typedef struct
{
    uint64_t created;
    uint64_t joined;
    uint64_t live;
    uint64_t create_failures;
    uint64_t join_underflows;
    uint32_t generation;
} RuntimeDiagnosticThreadSnapshot;

typedef struct
{
    bool process_memory_available;
    uint64_t process_memory_total;
    uint64_t process_memory_used;
    uint64_t process_memory_free;
    bool heap_available;
    uint64_t heap_arena;
    uint64_t heap_used;
    uint64_t heap_free;
    uint64_t heap_releasable;
    bool free_thread_slots_available;
    uint64_t free_thread_slots;
    uint64_t app_threads_created;
    uint64_t app_threads_joined;
    uint64_t app_threads_live;
    uint64_t app_thread_create_failures;
    uint64_t app_thread_join_underflows;
    uint64_t open_sockets;
    uint64_t open_sockets_by_subsystem[NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT];
    RuntimeDiagnosticThreadSnapshot threads[RUNTIME_DIAGNOSTIC_THREAD_COUNT];
} RuntimeDiagnosticResourceSnapshot;

bool runtime_diagnostics_enabled(void);
uint32_t runtime_diagnostics_thread_created(RuntimeDiagnosticThreadRole role);
void runtime_diagnostics_thread_create_failed(
    RuntimeDiagnosticThreadRole role);
bool runtime_diagnostics_thread_joined(RuntimeDiagnosticThreadRole role,
                                       uint32_t generation);
bool runtime_diagnostics_get_thread_snapshot(
    RuntimeDiagnosticThreadRole role,
    RuntimeDiagnosticThreadSnapshot *snapshot_out);
bool runtime_diagnostics_collect_resources(
    RuntimeDiagnosticResourceSnapshot *snapshot_out);
bool runtime_diagnostics_format_resource_snapshot(
    const RuntimeDiagnosticResourceSnapshot *snapshot, const char *event,
    const char *owner, uint32_t generation, char *output,
    size_t output_size);
bool runtime_diagnostics_reset(void);
const char *runtime_diagnostics_thread_name(
    RuntimeDiagnosticThreadRole role);

#endif
