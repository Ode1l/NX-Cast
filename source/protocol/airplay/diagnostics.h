#ifndef NXCAST_AIRPLAY_DIAGNOSTICS_H
#define NXCAST_AIRPLAY_DIAGNOSTICS_H

#include <stdint.h>

#include "app/network_diagnostics.h"
#include "app/runtime_diagnostics.h"
#include "protocol/airplay/trace.h"

#if defined(NXCAST_RUNTIME_OBSERVABILITY) && NXCAST_RUNTIME_OBSERVABILITY

#define AIRPLAY_OBSERVE(...) AIRPLAY_TRACE(__VA_ARGS__)
#define AIRPLAY_PACKET_TRACE(...) ((void)0)

static inline void airplay_diagnostics_log_thread(
    RuntimeDiagnosticThreadRole role, const char *event, uint32_t generation)
{
    RuntimeDiagnosticThreadSnapshot snapshot;

    if (!runtime_diagnostics_get_thread_snapshot(role, &snapshot))
        return;
    AIRPLAY_TRACE_SYNC(
        "[airplay-thread] role=%s event=%s generation=%u "
        "created=%llu joined=%llu live=%llu failed=%llu underflow=%llu\n",
        runtime_diagnostics_thread_name(role), event ? event : "unknown",
        generation, (unsigned long long)snapshot.created,
        (unsigned long long)snapshot.joined,
        (unsigned long long)snapshot.live,
        (unsigned long long)snapshot.create_failures,
        (unsigned long long)snapshot.join_underflows);
}

static inline uint32_t airplay_diagnostics_thread_created(
    RuntimeDiagnosticThreadRole role)
{
    uint32_t generation = runtime_diagnostics_thread_created(role);

    airplay_diagnostics_log_thread(role, "created", generation);
    return generation;
}

static inline void airplay_diagnostics_thread_create_failed(
    RuntimeDiagnosticThreadRole role)
{
    runtime_diagnostics_thread_create_failed(role);
    airplay_diagnostics_log_thread(role, "create-failed", 0u);
}

static inline void airplay_diagnostics_thread_joined(
    RuntimeDiagnosticThreadRole role, uint32_t generation)
{
    if (generation == 0u)
        return;
    (void)runtime_diagnostics_thread_joined(role, generation);
    airplay_diagnostics_log_thread(role, "joined", generation);
}

static inline void airplay_diagnostics_socket_opened(
    NetworkDiagnosticSubsystem subsystem)
{
    network_diagnostics_socket_opened(subsystem);
}

static inline void airplay_diagnostics_socket_closed(
    NetworkDiagnosticSubsystem subsystem)
{
    network_diagnostics_socket_closed(subsystem);
}

#else

#define AIRPLAY_OBSERVE(...) ((void)0)
#define AIRPLAY_PACKET_TRACE(...) AIRPLAY_TRACE(__VA_ARGS__)

static inline uint32_t airplay_diagnostics_thread_created(
    RuntimeDiagnosticThreadRole role)
{
    (void)role;
    return 0u;
}

static inline void airplay_diagnostics_thread_create_failed(
    RuntimeDiagnosticThreadRole role)
{
    (void)role;
}

static inline void airplay_diagnostics_thread_joined(
    RuntimeDiagnosticThreadRole role, uint32_t generation)
{
    (void)role;
    (void)generation;
}

static inline void airplay_diagnostics_socket_opened(
    NetworkDiagnosticSubsystem subsystem)
{
    (void)subsystem;
}

static inline void airplay_diagnostics_socket_closed(
    NetworkDiagnosticSubsystem subsystem)
{
    (void)subsystem;
}

#endif

#endif
