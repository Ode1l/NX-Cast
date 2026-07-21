#ifndef NXCAST_APP_PROTOCOL_COORDINATOR_H
#define NXCAST_APP_PROTOCOL_COORDINATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "player/core/ownership.h"

#define PROTOCOL_AIRPLAY_PIN_SIZE 5u
#define PROTOCOL_AIRPLAY_STATUS_MAX 96u

typedef enum
{
    PROTOCOL_COORDINATOR_STOPPED = 0,
    PROTOCOL_COORDINATOR_STARTING,
    PROTOCOL_COORDINATOR_READY,
    PROTOCOL_COORDINATOR_MEDIA_ACTIVE,
    PROTOCOL_COORDINATOR_STOPPING,
    PROTOCOL_COORDINATOR_FAILED
} ProtocolCoordinatorState;

typedef enum
{
    /* Service order defines startup order; shutdown runs in reverse. */
    PROTOCOL_SERVICE_IPTV = 0,
    PROTOCOL_SERVICE_DLNA,
    PROTOCOL_SERVICE_AIRPLAY,
    PROTOCOL_SERVICE_COUNT
} ProtocolService;

typedef enum
{
    PROTOCOL_SERVICE_DISABLED = 0,
    PROTOCOL_SERVICE_STOPPED,
    PROTOCOL_SERVICE_STARTING,
    PROTOCOL_SERVICE_RUNNING,
    PROTOCOL_SERVICE_FAILED
} ProtocolServiceState;

typedef struct
{
    bool enabled[PROTOCOL_SERVICE_COUNT];
    /* Diagnostic/low-contention mode: start one supervised worker at a time. */
    bool serial_startup;
} ProtocolCoordinatorConfig;

typedef struct
{
    bool running;
    bool starting;
    bool pin_visible;
    char pin[PROTOCOL_AIRPLAY_PIN_SIZE];
    char status[PROTOCOL_AIRPLAY_STATUS_MAX];
} ProtocolAirPlayStatus;

typedef struct
{
    ProtocolCoordinatorState state;
    ProtocolServiceState services[PROTOCOL_SERVICE_COUNT];
    bool service_worker_active[PROTOCOL_SERVICE_COUNT];
    uint64_t service_transition_age_ms[PROTOCOL_SERVICE_COUNT];
    ProtocolAirPlayStatus airplay;
    PlayerOwnershipLease active_media;
    uint32_t revision;
} ProtocolCoordinatorSnapshot;

typedef struct
{
    bool (*start)(void *context);
    /* Must only signal cancellation; stop runs after the start worker exits. */
    void (*request_stop)(void *context);
    void (*stop)(void *context);
    /* Use when a failed start can still leave resources requiring cleanup. */
    bool stop_after_start_attempt;
} ProtocolServiceOperations;

typedef struct
{
    void *context;
    ProtocolServiceOperations services[PROTOCOL_SERVICE_COUNT];
    bool (*airplay_get_status)(void *context,
                               ProtocolAirPlayStatus *status_out);
} ProtocolCoordinatorOperations;

typedef struct
{
    bool active;
    PlayerOwnershipLease lease;
    PlayerOwnershipLease previous;
} ProtocolMediaTransaction;

typedef struct
{
    bool active;
    PlayerOwnershipLease lease;
} ProtocolMediaGuard;

void protocol_coordinator_reset(void);
bool protocol_coordinator_start(const ProtocolCoordinatorConfig *config,
                                const ProtocolCoordinatorOperations *operations);
void protocol_coordinator_tick(void);
void protocol_coordinator_stop(void);
bool protocol_coordinator_begin_start(const ProtocolCoordinatorConfig *config);
bool protocol_coordinator_set_service_state(ProtocolService service,
                                            ProtocolServiceState state);
bool protocol_coordinator_begin_stop(void);
void protocol_coordinator_finish_stop(void);
bool protocol_coordinator_get_snapshot(ProtocolCoordinatorSnapshot *snapshot_out);

/* A successful begin returns a lease; actor execution validates it again. */
bool protocol_coordinator_media_begin(PlayerMediaOwner owner, uint64_t token,
                                      ProtocolMediaTransaction *transaction_out);
bool protocol_coordinator_media_validate(const PlayerOwnershipLease *lease);
bool protocol_coordinator_media_release(const PlayerOwnershipLease *lease);
void protocol_coordinator_media_abort(ProtocolMediaTransaction *transaction);
void protocol_coordinator_media_end(ProtocolMediaTransaction *transaction);
/* Guards are lease snapshots and never hold a lock across player submission. */
bool protocol_coordinator_media_guard_begin(PlayerMediaOwner owner, uint64_t token,
                                            ProtocolMediaGuard *guard_out);
bool protocol_coordinator_media_unowned_or_owner_guard_begin(
    PlayerMediaOwner owner, uint64_t token, ProtocolMediaGuard *guard_out);
bool protocol_coordinator_media_lease_guard_begin(
    const PlayerOwnershipLease *lease, bool allow_stopping,
    ProtocolMediaGuard *guard_out);
/* Teardown-only ownership snapshot when no owner validation is appropriate. */
bool protocol_coordinator_media_barrier_begin(ProtocolMediaGuard *guard_out);
void protocol_coordinator_media_guard_end(ProtocolMediaGuard *guard);
bool protocol_coordinator_media_current(PlayerOwnershipLease *lease_out);
bool protocol_coordinator_media_release_current(PlayerMediaOwner owner,
                                                uint64_t token);

const char *protocol_coordinator_state_name(ProtocolCoordinatorState state);
const char *protocol_service_name(ProtocolService service);
const char *protocol_service_state_name(ProtocolServiceState state);

#endif
