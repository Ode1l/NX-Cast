#include "player/types.h"

#include <stdlib.h>
#include <string.h>

static char *player_strdup_or_null(const char *value)
{
    if (!value)
        return NULL;
    return strdup(value);
}

void player_media_clear(PlayerMedia *media)
{
    if (!media)
        return;

    free(media->uri);
    free(media->metadata);
    media->uri = NULL;
    media->metadata = NULL;
}

bool player_media_copy(PlayerMedia *out, const PlayerMedia *media)
{
    PlayerMedia copy = {0};

    if (!out)
        return false;
    if (!media)
    {
        memset(out, 0, sizeof(*out));
        return true;
    }

    copy.uri = player_strdup_or_null(media->uri);
    if (media->uri && !copy.uri)
        goto fail;

    copy.metadata = player_strdup_or_null(media->metadata);
    if (media->metadata && !copy.metadata)
        goto fail;

    player_media_clear(out);
    *out = copy;
    return true;

fail:
    player_media_clear(&copy);
    return false;
}

bool player_media_set(PlayerMedia *media, const char *uri, const char *metadata)
{
    PlayerMedia copy = {0};

    if (!media)
        return false;

    copy.uri = player_strdup_or_null(uri);
    if (uri && !copy.uri)
        goto fail;

    copy.metadata = player_strdup_or_null(metadata);
    if (metadata && !copy.metadata)
        goto fail;

    player_media_clear(media);
    *media = copy;
    return true;

fail:
    player_media_clear(&copy);
    return false;
}

void player_event_clear(PlayerEvent *event)
{
    if (!event)
        return;

    free(event->uri);
    memset(event, 0, sizeof(*event));
}

bool player_event_copy(PlayerEvent *out, const PlayerEvent *event)
{
    PlayerEvent copy = {0};

    if (!out)
        return false;
    if (!event)
    {
        memset(out, 0, sizeof(*out));
        return true;
    }

    copy = *event;
    copy.uri = player_strdup_or_null(event->uri);
    if (event->uri && !copy.uri)
        return false;

    player_event_clear(out);
    *out = copy;
    return true;
}

void player_snapshot_clear(PlayerSnapshot *snapshot)
{
    if (!snapshot)
        return;

    player_media_clear(&snapshot->media);
    memset(snapshot, 0, sizeof(*snapshot));
}

bool player_snapshot_copy(PlayerSnapshot *out, const PlayerSnapshot *snapshot)
{
    PlayerSnapshot copy;

    if (!out || !snapshot)
        return false;

    memset(&copy, 0, sizeof(copy));
    copy.has_media = snapshot->has_media;
    copy.state = snapshot->state;
    copy.position_ms = snapshot->position_ms;
    copy.duration_ms = snapshot->duration_ms;
    copy.volume = snapshot->volume;
    copy.mute = snapshot->mute;
    copy.seekable = snapshot->seekable;

    if (snapshot->has_media && !player_media_copy(&copy.media, &snapshot->media))
        return false;

    player_snapshot_clear(out);
    *out = copy;
    return true;
}
