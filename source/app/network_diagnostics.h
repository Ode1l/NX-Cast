#ifndef NXCAST_APP_NETWORK_DIAGNOSTICS_H
#define NXCAST_APP_NETWORK_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_DIAGNOSTIC_ACTIVE_SLOT_COUNT 8u

typedef enum
{
    NETWORK_DIAGNOSTIC_MDNS = 0,
    NETWORK_DIAGNOSTIC_SSDP,
    NETWORK_DIAGNOSTIC_DLNA_HTTP,
    NETWORK_DIAGNOSTIC_AIRPLAY_CONTROL,
    NETWORK_DIAGNOSTIC_AIRPLAY_TIMING,
    NETWORK_DIAGNOSTIC_AIRPLAY_AUDIO,
    NETWORK_DIAGNOSTIC_AIRPLAY_MIRROR,
    NETWORK_DIAGNOSTIC_IPTV_BACKGROUND,
    NETWORK_DIAGNOSTIC_SUBSYSTEM_COUNT
} NetworkDiagnosticSubsystem;

typedef enum
{
    NETWORK_OPERATION_NONE = 0,
    NETWORK_OPERATION_SOCKET,
    NETWORK_OPERATION_CONNECT,
    NETWORK_OPERATION_SELECT,
    NETWORK_OPERATION_ACCEPT,
    NETWORK_OPERATION_RECV,
    NETWORK_OPERATION_SEND,
    NETWORK_OPERATION_HTTP_FETCH
} NetworkOperationKind;

typedef struct
{
    NetworkDiagnosticSubsystem subsystem;
    NetworkOperationKind operation;
    uint64_t token;
    uint64_t start_ms;
    int slot;
    bool active;
} NetworkOperationToken;

typedef struct
{
    uint64_t open_sockets;
    uint64_t sockets_opened;
    uint64_t sockets_closed;
    uint64_t socket_close_underflows;
    uint64_t active_operations;
    uint64_t operation_count;
    uint64_t operation_slot_overflows;
    uint64_t last_duration_ms;
    uint64_t maximum_duration_ms;
    uint64_t heartbeat_age_ms;
    uint64_t oldest_active_age_ms;
    uint64_t oldest_active_token;
    int last_error;
    NetworkOperationKind last_operation;
    NetworkOperationKind last_error_operation;
    NetworkOperationKind oldest_active_operation;
} NetworkDiagnosticSnapshot;

uint64_t network_diagnostics_now_ms(void);
void network_diagnostics_heartbeat(NetworkDiagnosticSubsystem subsystem);
void network_diagnostics_socket_opened(NetworkDiagnosticSubsystem subsystem);
void network_diagnostics_socket_closed(NetworkDiagnosticSubsystem subsystem);
NetworkOperationToken network_diagnostics_operation_begin(
    NetworkDiagnosticSubsystem subsystem, NetworkOperationKind operation);
void network_diagnostics_operation_end(NetworkOperationToken *token,
                                       int error_code);
bool network_diagnostics_get_snapshot(
    NetworkDiagnosticSubsystem subsystem,
    NetworkDiagnosticSnapshot *snapshot_out);
bool network_diagnostics_reset(void);
const char *network_diagnostics_subsystem_name(
    NetworkDiagnosticSubsystem subsystem);
const char *network_diagnostics_operation_name(NetworkOperationKind operation);

#endif
