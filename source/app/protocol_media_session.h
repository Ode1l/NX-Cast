#ifndef NXCAST_APP_PROTOCOL_MEDIA_SESSION_H
#define NXCAST_APP_PROTOCOL_MEDIA_SESSION_H

#include <stdint.h>

#include "player/core/ownership.h"

typedef enum
{
    PROTOCOL_MEDIA_SESSION_IDLE = 0,
    PROTOCOL_MEDIA_SESSION_PREPARING,
    PROTOCOL_MEDIA_SESSION_LOADING,
    PROTOCOL_MEDIA_SESSION_ACTIVE,
    PROTOCOL_MEDIA_SESSION_STOPPING,
    PROTOCOL_MEDIA_SESSION_FAILED
} ProtocolMediaSessionLifecycle;

typedef enum
{
    PROTOCOL_MEDIA_PLAYBACK_UNKNOWN = 0,
    PROTOCOL_MEDIA_PLAYBACK_PLAYING,
    PROTOCOL_MEDIA_PLAYBACK_PAUSED
} ProtocolMediaPlaybackState;

typedef enum
{
    PROTOCOL_MEDIA_CONTROL_UNKNOWN = 0,
    PROTOCOL_MEDIA_CONTROL_ATTACHED,
    PROTOCOL_MEDIA_CONTROL_DETACHED
} ProtocolMediaControlState;

typedef enum
{
    PROTOCOL_MEDIA_EVENT_CLAIMED = 0,
    PROTOCOL_MEDIA_EVENT_LOAD_REQUESTED,
    PROTOCOL_MEDIA_EVENT_ACTIVE,
    PROTOCOL_MEDIA_EVENT_PLAYING,
    PROTOCOL_MEDIA_EVENT_PAUSED,
    PROTOCOL_MEDIA_EVENT_CONTROL_ATTACHED,
    PROTOCOL_MEDIA_EVENT_CONTROL_DETACHED,
    PROTOCOL_MEDIA_EVENT_STOP_REQUESTED,
    PROTOCOL_MEDIA_EVENT_ENDED,
    PROTOCOL_MEDIA_EVENT_FAILED,
    PROTOCOL_MEDIA_EVENT_RELEASED,
    PROTOCOL_MEDIA_EVENT_RESET
} ProtocolMediaEventKind;

typedef struct
{
    ProtocolMediaEventKind kind;
    PlayerOwnershipLease lease;
} ProtocolMediaEvent;

typedef struct
{
    PlayerOwnershipLease lease;
    ProtocolMediaSessionLifecycle lifecycle;
    ProtocolMediaPlaybackState playback;
    ProtocolMediaControlState control;
    uint32_t revision;
} ProtocolMediaSessionSnapshot;

typedef enum
{
    PROTOCOL_MEDIA_TRANSITION_APPLIED = 0,
    PROTOCOL_MEDIA_TRANSITION_NO_CHANGE,
    PROTOCOL_MEDIA_TRANSITION_STALE,
    PROTOCOL_MEDIA_TRANSITION_INVALID
} ProtocolMediaTransitionStatus;

void protocol_media_session_init(ProtocolMediaSessionSnapshot *session);
ProtocolMediaTransitionStatus protocol_media_session_transition(
    ProtocolMediaSessionSnapshot *session, const ProtocolMediaEvent *event);

const char *protocol_media_session_lifecycle_name(
    ProtocolMediaSessionLifecycle lifecycle);
const char *protocol_media_playback_state_name(
    ProtocolMediaPlaybackState playback);
const char *protocol_media_control_state_name(ProtocolMediaControlState control);
const char *protocol_media_transition_status_name(
    ProtocolMediaTransitionStatus status);

#endif
