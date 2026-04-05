#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler_internal.h"
#include "player/types.h"

typedef enum
{
    DLNA_TRANSPORT_ACTION_PLAY = 1u << 0,
    DLNA_TRANSPORT_ACTION_STOP = 1u << 1,
    DLNA_TRANSPORT_ACTION_PAUSE = 1u << 2,
    DLNA_TRANSPORT_ACTION_SEEK = 1u << 3
} DlnaTransportActionMask;

// Map player-internal state to the smaller DLNA AVTransport state surface.
const char *dlna_transport_state_from_player_state(PlayerState state);
const char *dlna_transport_status_from_player_state(PlayerState state);

// DLNA time values are HH:MM:SS strings. We keep formatting here so SOAP and
// LastChange emit the exact same representation.
void dlna_format_hhmmss_from_ms(int value_ms, char *out, size_t out_size);

// Build a consistent protocol-facing snapshot from the player core plus the
// current SOAP runtime state when player snapshot data is incomplete.
void dlna_transport_get_snapshot(PlayerSnapshot *snapshot, const SoapRuntimeState *runtime_state);

// Compute transport actions once and let SOAP reads and GENA events share it.
unsigned int dlna_transport_current_actions(const PlayerSnapshot *snapshot);
bool dlna_transport_action_available(unsigned int actions, DlnaTransportActionMask action);
void dlna_transport_format_actions(unsigned int actions, char *out, size_t out_size);

// Keep SOAP runtime state aligned with player events/snapshots.
void dlna_transport_sync_runtime_from_snapshot(SoapRuntimeState *runtime_state);
void dlna_transport_sync_runtime_on_event(SoapRuntimeState *runtime_state, const PlayerEvent *event);
