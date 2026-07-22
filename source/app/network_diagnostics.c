#if !defined(__SWITCH__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "network_diagnostics.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#define NETWORK_DIAGNOSTIC_SLOT_RESERVED UINT64_MAX

typedef struct
{
    atomic_uint_fast64_t token;
    atomic_uint_fast64_t start_ms;
    atomic_int operation;
} NetworkDiagnosticActiveSlot;

typedef struct
{
    atomic_uint_fast64_t open_sockets;
    atomic_uint_fast64_t sockets_opened;
    atomic_uint_fast64_t sockets_closed;
    atomic_uint_fast64_t socket_close_underflows;
    atomic_uint_fast64_t active_operations;
    atomic_uint_fast64_t operation_count;
    atomic_uint_fast64_t operation_slot_overflows;
    atomic_uint_fast64_t last_duration_ms;
    atomic_uint_fast64_t maximum_duration_ms;
    atomic_uint_fast64_t heartbeat_ms;
    atomic_int last_error;
    atomic_int last_operation;
    atomic_int last_error_operation;
    NetworkDiagnosticActiveSlot active[NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT];
} NetworkDiagnosticState;

static NetworkDiagnosticState
    g_network_diagnostics[NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT];
static atomic_uint_fast64_t g_network_diagnostic_next_token;

static bool network_diagnostics_valid_subsystem(
    NetworkDiagnosticSubsystem subsystem)
{
    return subsystem >= NETWORK_DIAGNOSTIC_MDNS &&
           subsystem < NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT;
}

uint64_t network_diagnostics_now_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / UINT64_C(1000000);
#else
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
#endif
}

void network_diagnostics_heartbeat(NetworkDiagnosticSubsystem subsystem)
{
    if (!network_diagnostics_valid_subsystem(subsystem))
        return;
    atomic_store(&g_network_diagnostics[subsystem].heartbeat_ms,
                 network_diagnostics_now_ms());
}

void network_diagnostics_socket_opened(NetworkDiagnosticSubsystem subsystem)
{
    NetworkDiagnosticState *state;

    if (!network_diagnostics_valid_subsystem(subsystem))
        return;
    state = &g_network_diagnostics[subsystem];
    atomic_fetch_add(&state->open_sockets, 1u);
    atomic_fetch_add(&state->sockets_opened, 1u);
    network_diagnostics_heartbeat(subsystem);
}

void network_diagnostics_socket_closed(NetworkDiagnosticSubsystem subsystem)
{
    NetworkDiagnosticState *state;
    uint_fast64_t current;

    if (!network_diagnostics_valid_subsystem(subsystem))
        return;
    state = &g_network_diagnostics[subsystem];
    current = atomic_load(&state->open_sockets);
    while (current > 0u &&
           !atomic_compare_exchange_weak(&state->open_sockets, &current,
                                         current - 1u))
    {
    }
    if (current == 0u)
        atomic_fetch_add(&state->socket_close_underflows, 1u);
    else
        atomic_fetch_add(&state->sockets_closed, 1u);
    network_diagnostics_heartbeat(subsystem);
}

NetworkOperationToken network_diagnostics_operation_begin(
    NetworkDiagnosticSubsystem subsystem, NetworkOperationKind operation)
{
    NetworkOperationToken result = {0};
    NetworkDiagnosticState *state;
    uint_fast64_t identifier;

    result.slot = -1;
    if (!network_diagnostics_valid_subsystem(subsystem) ||
        operation <= NETWORK_OPERATION_NONE ||
        operation > NETWORK_OPERATION_HTTP_FETCH)
        return result;

    state = &g_network_diagnostics[subsystem];
    do
    {
        identifier = atomic_fetch_add(&g_network_diagnostic_next_token, 1u) + 1u;
    } while (identifier == 0u || identifier == NETWORK_DIAGNOSTIC_SLOT_RESERVED);

    result.subsystem = subsystem;
    result.operation = operation;
    result.token = (uint64_t)identifier;
    result.start_ms = network_diagnostics_now_ms();
    result.active = true;
    atomic_fetch_add(&state->active_operations, 1u);
    atomic_fetch_add(&state->operation_count, 1u);
    atomic_store(&state->heartbeat_ms, result.start_ms);

    for (size_t index = 0u; index < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT;
         ++index)
    {
        uint_fast64_t expected = 0u;
        NetworkDiagnosticActiveSlot *slot = &state->active[index];

        if (!atomic_compare_exchange_strong(&slot->token, &expected,
                                            NETWORK_DIAGNOSTIC_SLOT_RESERVED))
            continue;
        atomic_store(&slot->start_ms, result.start_ms);
        atomic_store(&slot->operation, (int)operation);
        atomic_store(&slot->token, identifier);
        result.slot = (int)index;
        break;
    }
    if (result.slot < 0)
        atomic_fetch_add(&state->operation_slot_overflows, 1u);
    return result;
}

static void network_diagnostics_update_max(atomic_uint_fast64_t *maximum,
                                           uint_fast64_t value)
{
    uint_fast64_t current = atomic_load(maximum);

    while (current < value &&
           !atomic_compare_exchange_weak(maximum, &current, value))
    {
    }
}

void network_diagnostics_operation_end(NetworkOperationToken *token,
                                       int error_code)
{
    NetworkDiagnosticState *state;
    uint64_t now_ms;
    uint_fast64_t duration_ms;
    bool counted = true;

    if (!token || !token->active ||
        !network_diagnostics_valid_subsystem(token->subsystem))
        return;
    state = &g_network_diagnostics[token->subsystem];
    if (token->slot >= 0 &&
        (unsigned)token->slot < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT)
    {
        NetworkDiagnosticActiveSlot *slot = &state->active[token->slot];
        uint_fast64_t expected = (uint_fast64_t)token->token;

        counted = atomic_compare_exchange_strong(&slot->token, &expected, 0u);
    }
    token->active = false;
    if (!counted)
        return;

    now_ms = network_diagnostics_now_ms();
    duration_ms = now_ms >= token->start_ms ? now_ms - token->start_ms : 0u;
    atomic_store(&state->last_duration_ms, duration_ms);
    atomic_store(&state->last_operation, (int)token->operation);
    network_diagnostics_update_max(&state->maximum_duration_ms, duration_ms);
    if (error_code != 0)
    {
        atomic_store(&state->last_error, error_code);
        atomic_store(&state->last_error_operation, (int)token->operation);
    }
    atomic_fetch_sub(&state->active_operations, 1u);
    atomic_store(&state->heartbeat_ms, now_ms);
}

bool network_diagnostics_get_snapshot(
    NetworkDiagnosticSubsystem subsystem,
    NetworkDiagnosticSnapshot *snapshot_out)
{
    NetworkDiagnosticState *state;
    uint64_t now_ms;
    uint64_t heartbeat_ms;
    uint64_t oldest_start_ms = 0u;

    if (!network_diagnostics_valid_subsystem(subsystem) || !snapshot_out)
        return false;
    state = &g_network_diagnostics[subsystem];
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->open_sockets = atomic_load(&state->open_sockets);
    snapshot_out->sockets_opened = atomic_load(&state->sockets_opened);
    snapshot_out->sockets_closed = atomic_load(&state->sockets_closed);
    snapshot_out->socket_close_underflows =
        atomic_load(&state->socket_close_underflows);
    snapshot_out->active_operations = atomic_load(&state->active_operations);
    snapshot_out->operation_count = atomic_load(&state->operation_count);
    snapshot_out->operation_slot_overflows =
        atomic_load(&state->operation_slot_overflows);
    snapshot_out->last_duration_ms = atomic_load(&state->last_duration_ms);
    snapshot_out->maximum_duration_ms = atomic_load(&state->maximum_duration_ms);
    snapshot_out->last_error = atomic_load(&state->last_error);
    snapshot_out->last_operation =
        (NetworkOperationKind)atomic_load(&state->last_operation);
    snapshot_out->last_error_operation =
        (NetworkOperationKind)atomic_load(&state->last_error_operation);

    for (size_t index = 0u; index < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT;
         ++index)
    {
        NetworkDiagnosticActiveSlot *slot = &state->active[index];
        uint64_t identifier = atomic_load(&slot->token);
        uint64_t start_ms;
        NetworkOperationKind operation;

        if (identifier == 0u || identifier == NETWORK_DIAGNOSTIC_SLOT_RESERVED)
            continue;
        start_ms = atomic_load(&slot->start_ms);
        operation = (NetworkOperationKind)atomic_load(&slot->operation);
        if (atomic_load(&slot->token) != identifier)
            continue;
        if (oldest_start_ms == 0u || start_ms < oldest_start_ms)
        {
            oldest_start_ms = start_ms;
            snapshot_out->oldest_active_token = identifier;
            snapshot_out->oldest_active_operation = operation;
        }
    }

    now_ms = network_diagnostics_now_ms();
    heartbeat_ms = atomic_load(&state->heartbeat_ms);
    snapshot_out->heartbeat_age_ms =
        heartbeat_ms > 0u && now_ms >= heartbeat_ms ? now_ms - heartbeat_ms : 0u;
    snapshot_out->oldest_active_age_ms =
        oldest_start_ms > 0u && now_ms >= oldest_start_ms
            ? now_ms - oldest_start_ms
            : 0u;
    return true;
}

bool network_diagnostics_reset(void)
{
    for (size_t subsystem = 0u;
         subsystem < NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT; ++subsystem)
    {
        NetworkDiagnosticState *state = &g_network_diagnostics[subsystem];

        if (atomic_load(&state->open_sockets) != 0u ||
            atomic_load(&state->active_operations) != 0u)
            return false;
    }
    for (size_t subsystem = 0u;
         subsystem < NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT; ++subsystem)
    {
        NetworkDiagnosticState *state = &g_network_diagnostics[subsystem];

        atomic_store(&state->open_sockets, 0u);
        atomic_store(&state->sockets_opened, 0u);
        atomic_store(&state->sockets_closed, 0u);
        atomic_store(&state->socket_close_underflows, 0u);
        atomic_store(&state->active_operations, 0u);
        atomic_store(&state->operation_count, 0u);
        atomic_store(&state->operation_slot_overflows, 0u);
        atomic_store(&state->last_duration_ms, 0u);
        atomic_store(&state->maximum_duration_ms, 0u);
        atomic_store(&state->heartbeat_ms, 0u);
        atomic_store(&state->last_error, 0);
        atomic_store(&state->last_operation, NETWORK_OPERATION_NONE);
        atomic_store(&state->last_error_operation, NETWORK_OPERATION_NONE);
        for (size_t slot = 0u; slot < NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT;
             ++slot)
        {
            atomic_store(&state->active[slot].token, 0u);
            atomic_store(&state->active[slot].start_ms, 0u);
            atomic_store(&state->active[slot].operation,
                         NETWORK_OPERATION_NONE);
        }
    }
    atomic_store(&g_network_diagnostic_next_token, 0u);
    return true;
}

const char *network_diagnostics_subsystem_name(
    NetworkDiagnosticSubsystem subsystem)
{
    switch (subsystem)
    {
    case NETWORK_DIAGNOSTIC_MDNS:
        return "mdns";
    case NETWORK_DIAGNOSTIC_SSDP:
        return "ssdp";
    case NETWORK_DIAGNOSTIC_DLNA_HTTP:
        return "dlna-http";
    case NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL:
        return "airplay-control";
    case NETWORK_DIAGNOSTIC_AIRPLAY_TIMING:
        return "airplay-timing";
    case NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO:
        return "airplay-audio";
    case NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR:
        return "airplay-mirror";
    case NETWORK_DIAGNOSTIC_IPTV_BACKGROUND:
        return "iptv-background";
    default:
        return "unknown";
    }
}

const char *network_diagnostics_operation_name(NetworkOperationKind operation)
{
    switch (operation)
    {
    case NETWORK_OPERATION_SOCKET:
        return "socket";
    case NETWORK_OPERATION_CONNECT:
        return "connect";
    case NETWORK_OPERATION_SELECT:
        return "select";
    case NETWORK_OPERATION_ACCEPT:
        return "accept";
    case NETWORK_OPERATION_RECV:
        return "recv";
    case NETWORK_OPERATION_SEND:
        return "send";
    case NETWORK_OPERATION_HTTP_FETCH:
        return "http-fetch";
    case NETWORK_OPERATION_NONE:
    default:
        return "none";
    }
}
