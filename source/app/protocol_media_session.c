#include "protocol_media_session.h"

#include <stdbool.h>
#include <string.h>

static bool lease_valid(const PlayerOwnershipLease *lease)
{
    return lease && lease->owner != PLAYER_MEDIA_OWNER_NONE &&
           lease->generation != 0u;
}

static bool lease_equal(const PlayerOwnershipLease *left,
                        const PlayerOwnershipLease *right)
{
    return left->owner == right->owner && left->token == right->token &&
           left->generation == right->generation;
}

static bool generation_newer(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) > 0;
}

static void bump_revision(ProtocolMediaSessionSnapshot *session)
{
    ++session->revision;
    if (session->revision == 0u)
        ++session->revision;
}

static ProtocolMediaTransitionStatus apply_claim(
    ProtocolMediaSessionSnapshot *session, const PlayerOwnershipLease *lease)
{
    if (!lease_valid(lease))
        return PROTOCOL_MEDIA_TRANSITION_INVALID;
    if (lease_equal(&session->lease, lease))
        return PROTOCOL_MEDIA_TRANSITION_NO_CHANGE;
    if (session->lease.generation != 0u &&
        !generation_newer(lease->generation, session->lease.generation))
        return PROTOCOL_MEDIA_TRANSITION_STALE;

    session->lease = *lease;
    session->lifecycle = PROTOCOL_MEDIA_SESSION_PREPARING;
    session->playback = PROTOCOL_MEDIA_PLAYBACK_UNKNOWN;
    session->control = PROTOCOL_MEDIA_CONTROL_UNKNOWN;
    bump_revision(session);
    return PROTOCOL_MEDIA_TRANSITION_APPLIED;
}

static ProtocolMediaTransitionStatus apply_reset(
    ProtocolMediaSessionSnapshot *session)
{
    uint32_t revision;

    if (session->lifecycle == PROTOCOL_MEDIA_SESSION_IDLE &&
        session->lease.owner == PLAYER_MEDIA_OWNER_NONE)
        return PROTOCOL_MEDIA_TRANSITION_NO_CHANGE;
    revision = session->revision;
    memset(session, 0, sizeof(*session));
    session->lifecycle = PROTOCOL_MEDIA_SESSION_IDLE;
    session->revision = revision;
    bump_revision(session);
    return PROTOCOL_MEDIA_TRANSITION_APPLIED;
}

void protocol_media_session_init(ProtocolMediaSessionSnapshot *session)
{
    if (!session)
        return;
    memset(session, 0, sizeof(*session));
    session->lifecycle = PROTOCOL_MEDIA_SESSION_IDLE;
}

ProtocolMediaTransitionStatus protocol_media_session_transition(
    ProtocolMediaSessionSnapshot *session, const ProtocolMediaEvent *event)
{
    bool changed = false;

    if (!session || !event)
        return PROTOCOL_MEDIA_TRANSITION_INVALID;
    if (event->kind == PROTOCOL_MEDIA_EVENT_RESET)
        return apply_reset(session);
    if (event->kind == PROTOCOL_MEDIA_EVENT_CLAIMED)
        return apply_claim(session, &event->lease);
    if (!lease_valid(&event->lease) ||
        !lease_equal(&session->lease, &event->lease))
        return PROTOCOL_MEDIA_TRANSITION_STALE;

    switch (event->kind)
    {
    case PROTOCOL_MEDIA_EVENT_LOAD_REQUESTED:
        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING ||
            session->lifecycle == PROTOCOL_MEDIA_SESSION_FAILED)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_LOADING ||
                  session->playback != PROTOCOL_MEDIA_PLAYBACK_UNKNOWN;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_LOADING;
        session->playback = PROTOCOL_MEDIA_PLAYBACK_UNKNOWN;
        break;
    case PROTOCOL_MEDIA_EVENT_ACTIVE:
        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING ||
            session->lifecycle == PROTOCOL_MEDIA_SESSION_FAILED)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_ACTIVE;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_ACTIVE;
        break;
    case PROTOCOL_MEDIA_EVENT_PLAYING:
        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING ||
            session->lifecycle == PROTOCOL_MEDIA_SESSION_FAILED)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_ACTIVE ||
                  session->playback != PROTOCOL_MEDIA_PLAYBACK_PLAYING;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_ACTIVE;
        session->playback = PROTOCOL_MEDIA_PLAYBACK_PLAYING;
        break;
    case PROTOCOL_MEDIA_EVENT_PAUSED:
        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING ||
            session->lifecycle == PROTOCOL_MEDIA_SESSION_FAILED)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_ACTIVE ||
                  session->playback != PROTOCOL_MEDIA_PLAYBACK_PAUSED;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_ACTIVE;
        session->playback = PROTOCOL_MEDIA_PLAYBACK_PAUSED;
        break;
    case PROTOCOL_MEDIA_EVENT_CONTROL_ATTACHED:
    case PROTOCOL_MEDIA_EVENT_CONTROL_DETACHED:
    {
        ProtocolMediaControlState control =
            event->kind == PROTOCOL_MEDIA_EVENT_CONTROL_ATTACHED
                ? PROTOCOL_MEDIA_CONTROL_ATTACHED
                : PROTOCOL_MEDIA_CONTROL_DETACHED;

        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING ||
            session->lifecycle == PROTOCOL_MEDIA_SESSION_FAILED)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->control != control;
        session->control = control;
        break;
    }
    case PROTOCOL_MEDIA_EVENT_STOP_REQUESTED:
    case PROTOCOL_MEDIA_EVENT_ENDED:
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_STOPPING;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_STOPPING;
        break;
    case PROTOCOL_MEDIA_EVENT_FAILED:
        if (session->lifecycle == PROTOCOL_MEDIA_SESSION_STOPPING)
            return PROTOCOL_MEDIA_TRANSITION_INVALID;
        changed = session->lifecycle != PROTOCOL_MEDIA_SESSION_FAILED;
        session->lifecycle = PROTOCOL_MEDIA_SESSION_FAILED;
        break;
    case PROTOCOL_MEDIA_EVENT_RELEASED:
        return apply_reset(session);
    case PROTOCOL_MEDIA_EVENT_CLAIMED:
    case PROTOCOL_MEDIA_EVENT_RESET:
    default:
        return PROTOCOL_MEDIA_TRANSITION_INVALID;
    }

    if (!changed)
        return PROTOCOL_MEDIA_TRANSITION_NO_CHANGE;
    bump_revision(session);
    return PROTOCOL_MEDIA_TRANSITION_APPLIED;
}

const char *protocol_media_session_lifecycle_name(
    ProtocolMediaSessionLifecycle lifecycle)
{
    switch (lifecycle)
    {
    case PROTOCOL_MEDIA_SESSION_IDLE:
        return "idle";
    case PROTOCOL_MEDIA_SESSION_PREPARING:
        return "preparing";
    case PROTOCOL_MEDIA_SESSION_LOADING:
        return "loading";
    case PROTOCOL_MEDIA_SESSION_ACTIVE:
        return "active";
    case PROTOCOL_MEDIA_SESSION_STOPPING:
        return "stopping";
    case PROTOCOL_MEDIA_SESSION_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

const char *protocol_media_transition_status_name(
    ProtocolMediaTransitionStatus status)
{
    switch (status)
    {
    case PROTOCOL_MEDIA_TRANSITION_APPLIED:
        return "applied";
    case PROTOCOL_MEDIA_TRANSITION_NO_CHANGE:
        return "no-change";
    case PROTOCOL_MEDIA_TRANSITION_STALE:
        return "stale";
    case PROTOCOL_MEDIA_TRANSITION_INVALID:
    default:
        return "invalid";
    }
}

const char *protocol_media_playback_state_name(
    ProtocolMediaPlaybackState playback)
{
    switch (playback)
    {
    case PROTOCOL_MEDIA_PLAYBACK_UNKNOWN:
        return "unknown";
    case PROTOCOL_MEDIA_PLAYBACK_PLAYING:
        return "playing";
    case PROTOCOL_MEDIA_PLAYBACK_PAUSED:
        return "paused";
    default:
        return "invalid";
    }
}

const char *protocol_media_control_state_name(ProtocolMediaControlState control)
{
    switch (control)
    {
    case PROTOCOL_MEDIA_CONTROL_UNKNOWN:
        return "unknown";
    case PROTOCOL_MEDIA_CONTROL_ATTACHED:
        return "attached";
    case PROTOCOL_MEDIA_CONTROL_DETACHED:
        return "detached";
    default:
        return "invalid";
    }
}
