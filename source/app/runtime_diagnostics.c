#include "runtime_diagnostics.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifdef __SWITCH__
#include <malloc.h>
#include <switch.h>
#endif

typedef struct
{
    atomic_uint_fast64_t created;
    atomic_uint_fast64_t joined;
    atomic_uint_fast64_t live;
    atomic_uint_fast64_t create_failures;
    atomic_uint_fast64_t join_underflows;
    atomic_uint_fast32_t generation;
} RuntimeDiagnosticThreadState;

static RuntimeDiagnosticThreadState
    g_runtime_threads[RUNTIME_DIAGNOSTIC_THREAD_COUNT];

static bool runtime_diagnostics_valid_role(RuntimeDiagnosticThreadRole role)
{
    return role >= RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS &&
           role < RUNTIME_DIAGNOSTIC_THREAD_COUNT;
}

bool runtime_diagnostics_enabled(void)
{
#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY
    return true;
#else
    return false;
#endif
}

uint32_t runtime_diagnostics_thread_created(RuntimeDiagnosticThreadRole role)
{
    RuntimeDiagnosticThreadState *state;
    uint32_t generation;

    if (!runtime_diagnostics_valid_role(role))
        return 0u;
    state = &g_runtime_threads[role];
    generation =
        (uint32_t)(atomic_fetch_add(&state->generation, 1u) + 1u);
    if (generation == 0u)
        generation =
            (uint32_t)(atomic_fetch_add(&state->generation, 1u) + 1u);
    atomic_fetch_add(&state->created, 1u);
    atomic_fetch_add(&state->live, 1u);
    return generation;
}

void runtime_diagnostics_thread_create_failed(
    RuntimeDiagnosticThreadRole role)
{
    if (!runtime_diagnostics_valid_role(role))
        return;
    atomic_fetch_add(&g_runtime_threads[role].create_failures, 1u);
}

bool runtime_diagnostics_thread_joined(RuntimeDiagnosticThreadRole role,
                                       uint32_t generation)
{
    RuntimeDiagnosticThreadState *state;
    uint_fast64_t live;

    if (!runtime_diagnostics_valid_role(role) || generation == 0u)
        return false;
    state = &g_runtime_threads[role];
    live = atomic_load(&state->live);
    while (live > 0u &&
           !atomic_compare_exchange_weak(&state->live, &live, live - 1u))
    {
    }
    if (live == 0u)
    {
        atomic_fetch_add(&state->join_underflows, 1u);
        return false;
    }
    atomic_fetch_add(&state->joined, 1u);
    return true;
}

bool runtime_diagnostics_get_thread_snapshot(
    RuntimeDiagnosticThreadRole role,
    RuntimeDiagnosticThreadSnapshot *snapshot_out)
{
    RuntimeDiagnosticThreadState *state;

    if (!runtime_diagnostics_valid_role(role) || !snapshot_out)
        return false;
    state = &g_runtime_threads[role];
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->created = atomic_load(&state->created);
    snapshot_out->joined = atomic_load(&state->joined);
    snapshot_out->live = atomic_load(&state->live);
    snapshot_out->create_failures = atomic_load(&state->create_failures);
    snapshot_out->join_underflows = atomic_load(&state->join_underflows);
    snapshot_out->generation = (uint32_t)atomic_load(&state->generation);
    return true;
}

static void runtime_diagnostics_collect_process_resources(
    RuntimeDiagnosticResourceSnapshot *snapshot)
{
#ifdef __SWITCH__
    uint64_t total = 0u;
    uint64_t used = 0u;
    uint64_t free_slots = 0u;
    struct mallinfo heap = mallinfo();

    if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize,
                               CUR_PROCESS_HANDLE, 0u)) &&
        R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize,
                               CUR_PROCESS_HANDLE, 0u)))
    {
        snapshot->process_memory_available = true;
        snapshot->process_memory_total = total;
        snapshot->process_memory_used = used;
        snapshot->process_memory_free = total >= used ? total - used : 0u;
    }
    snapshot->heap_available = true;
    snapshot->heap_arena = heap.arena > 0 ? (uint64_t)heap.arena : 0u;
    snapshot->heap_used = heap.uordblks > 0 ? (uint64_t)heap.uordblks : 0u;
    snapshot->heap_free = heap.fordblks > 0 ? (uint64_t)heap.fordblks : 0u;
    snapshot->heap_releasable =
        heap.keepcost > 0 ? (uint64_t)heap.keepcost : 0u;
    if (R_SUCCEEDED(svcGetInfo(&free_slots, InfoType_FreeThreadCount,
                               CUR_PROCESS_HANDLE, 0u)))
    {
        snapshot->free_thread_slots_available = true;
        snapshot->free_thread_slots = free_slots;
    }
#else
    (void)snapshot;
#endif
}

bool runtime_diagnostics_collect_resources(
    RuntimeDiagnosticResourceSnapshot *snapshot_out)
{
    if (!snapshot_out)
        return false;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    runtime_diagnostics_collect_process_resources(snapshot_out);

    for (int role = 0; role < RUNTIME_DIAGNOSTIC_THREAD_COUNT; ++role)
    {
        RuntimeDiagnosticThreadSnapshot *thread = &snapshot_out->threads[role];

        if (!runtime_diagnostics_get_thread_snapshot(
                (RuntimeDiagnosticThreadRole)role, thread))
            continue;
        snapshot_out->app_threads_created += thread->created;
        snapshot_out->app_threads_joined += thread->joined;
        snapshot_out->app_threads_live += thread->live;
        snapshot_out->app_thread_create_failures += thread->create_failures;
        snapshot_out->app_thread_join_underflows += thread->join_underflows;
    }
    for (int subsystem = 0; subsystem < NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT;
         ++subsystem)
    {
        NetworkDiagnosticSnapshot network;

        if (!network_diagnostics_get_snapshot(
                (NetworkDiagnosticSubsystem)subsystem, &network))
            continue;
        snapshot_out->open_sockets_by_subsystem[subsystem] =
            network.open_sockets;
        snapshot_out->open_sockets += network.open_sockets;
    }
    return true;
}

static bool runtime_diagnostics_append(char *output, size_t output_size,
                                       size_t *used, const char *format, ...)
{
    va_list arguments;
    int written;

    if (!output || !used || *used >= output_size)
        return false;
    va_start(arguments, format);
    written = vsnprintf(output + *used, output_size - *used, format,
                        arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= output_size - *used)
    {
        output[output_size - 1u] = '\0';
        *used = output_size - 1u;
        return false;
    }
    *used += (size_t)written;
    return true;
}

bool runtime_diagnostics_format_resource_snapshot(
    const RuntimeDiagnosticResourceSnapshot *snapshot, const char *event,
    const char *owner, uint32_t generation, char *output,
    size_t output_size)
{
    size_t used = 0u;
    bool complete;

    if (!snapshot || !output || output_size == 0u)
        return false;
    output[0] = '\0';
    complete = runtime_diagnostics_append(
        output, output_size, &used,
        "event=%s owner=%s generation=%u app_threads=%llu/%llu/%llu/%llu/%llu ",
        event ? event : "unknown", owner ? owner : "unknown", generation,
        (unsigned long long)snapshot->app_threads_live,
        (unsigned long long)snapshot->app_threads_created,
        (unsigned long long)snapshot->app_threads_joined,
        (unsigned long long)snapshot->app_thread_create_failures,
        (unsigned long long)snapshot->app_thread_join_underflows);
    if (!complete)
        return false;

    if (snapshot->free_thread_slots_available)
        complete = runtime_diagnostics_append(
            output, output_size, &used, "free_thread_slots=%llu ",
            (unsigned long long)snapshot->free_thread_slots);
    else
        complete = runtime_diagnostics_append(
            output, output_size, &used, "free_thread_slots=unknown ");
    if (!complete)
        return false;

    if (snapshot->process_memory_available)
        complete = runtime_diagnostics_append(
            output, output_size, &used, "mem=%llu/%llu/%llu ",
            (unsigned long long)snapshot->process_memory_used,
            (unsigned long long)snapshot->process_memory_total,
            (unsigned long long)snapshot->process_memory_free);
    else
        complete = runtime_diagnostics_append(output, output_size, &used,
                                              "mem=unknown ");
    if (!complete)
        return false;

    if (snapshot->heap_available)
        complete = runtime_diagnostics_append(
            output, output_size, &used, "heap=%llu/%llu/%llu/%llu ",
            (unsigned long long)snapshot->heap_used,
            (unsigned long long)snapshot->heap_arena,
            (unsigned long long)snapshot->heap_free,
            (unsigned long long)snapshot->heap_releasable);
    else
        complete = runtime_diagnostics_append(output, output_size, &used,
                                              "heap=unknown ");
    if (!complete)
        return false;

    complete = runtime_diagnostics_append(
        output, output_size, &used, "sockets=%llu",
        (unsigned long long)snapshot->open_sockets);
    for (int subsystem = 0;
         complete && subsystem < NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT;
         ++subsystem)
    {
        complete = runtime_diagnostics_append(
            output, output_size, &used, " %s=%llu",
            network_diagnostics_subsystem_name(
                (NetworkDiagnosticSubsystem)subsystem),
            (unsigned long long)
                snapshot->open_sockets_by_subsystem[subsystem]);
    }
    return complete;
}

bool runtime_diagnostics_reset(void)
{
    for (int role = 0; role < RUNTIME_DIAGNOSTIC_THREAD_COUNT; ++role)
    {
        if (atomic_load(&g_runtime_threads[role].live) != 0u)
            return false;
    }
    for (int role = 0; role < RUNTIME_DIAGNOSTIC_THREAD_COUNT; ++role)
    {
        RuntimeDiagnosticThreadState *state = &g_runtime_threads[role];

        atomic_store(&state->created, 0u);
        atomic_store(&state->joined, 0u);
        atomic_store(&state->live, 0u);
        atomic_store(&state->create_failures, 0u);
        atomic_store(&state->join_underflows, 0u);
        atomic_store(&state->generation, 0u);
    }
    return true;
}

const char *runtime_diagnostics_thread_name(RuntimeDiagnosticThreadRole role)
{
    switch (role)
    {
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MDNS:
        return "airplay-mdns";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_LISTENER:
        return "airplay-listener";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_CLIENT:
        return "airplay-client";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_TIMING:
        return "airplay-timing";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_AUDIO:
        return "airplay-audio";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR:
        return "airplay-mirror";
    case RUNTIME_DIAGNOSTIC_THREAD_AIRPLAY_MIRROR_RUNTIME:
        return "airplay-mirror-runtime";
    case RUNTIME_DIAGNOSTIC_THREAD_COUNT:
    default:
        return "unknown";
    }
}
