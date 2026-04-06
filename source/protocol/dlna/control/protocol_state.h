#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "handler.h"
#include "player/player.h"

typedef struct
{
    char transport_uri[SOAP_TRANSPORT_URI_MAX];
    char transport_uri_metadata[SOAP_TRANSPORT_METADATA_MAX];
    char transport_state[32];
    char transport_status[16];
    char transport_speed[16];
    char transport_duration[16];
    char transport_rel_time[16];
    char transport_abs_time[16];
    int volume;
    bool mute;
    char source_protocol_info[256];
    char sink_protocol_info[2048];
    char connection_ids[16];
} DlnaProtocolState;

// Macast-style single source of truth for protocol-observed state. SOAP
// actions, LastChange/GENA, and compatibility helpers all read from here.
void dlna_protocol_state_init(void);
void dlna_protocol_state_reset(void);
void dlna_protocol_state_sync_from_player(void);
void dlna_protocol_state_on_player_event(const PlayerEvent *event);
void dlna_protocol_state_set_transport_uri(const char *uri, const char *metadata);
void dlna_protocol_state_set_transport_status(const char *status);
void dlna_protocol_state_set_transport_speed(const char *speed);
void dlna_protocol_state_set_transport_timing(int duration_ms, int position_ms);
void dlna_protocol_state_set_source_protocol_info(const char *value);
void dlna_protocol_state_set_sink_protocol_info(const char *value);
void dlna_protocol_state_set_connection_ids(const char *value);
const DlnaProtocolState *dlna_protocol_state_view(void);

const char *dlna_protocol_transport_state_from_player_state(PlayerState state);
const char *dlna_protocol_transport_status_from_player_state(PlayerState state);
void dlna_protocol_format_hhmmss_from_ms(int value_ms, char *out, size_t out_size);
