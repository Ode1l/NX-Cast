#include "player/types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_media_lifecycle(void)
{
    PlayerMedia media = {0};
    PlayerMedia copy = {0};

    assert(player_media_set(&media, "https://example.test/video", "metadata"));
    assert(player_media_copy(&copy, &media));
    assert(copy.uri && copy.metadata);
    assert(copy.uri != media.uri && copy.metadata != media.metadata);
    assert(strcmp(copy.uri, media.uri) == 0);
    assert(strcmp(copy.metadata, media.metadata) == 0);

    assert(player_media_copy(&copy, &copy));
    assert(strcmp(copy.uri, "https://example.test/video") == 0);
    assert(player_media_copy(&copy, NULL));
    assert(copy.uri == NULL && copy.metadata == NULL);
    assert(player_media_set(&media, NULL, NULL));
    assert(media.uri == NULL && media.metadata == NULL);

    player_media_clear(&copy);
    player_media_clear(&media);
}

static void test_event_lifecycle(void)
{
    PlayerEvent event = {
        .type = PLAYER_EVENT_URI_CHANGED,
        .state = PLAYER_STATE_PLAYING,
        .uri = NULL,
    };
    PlayerEvent copy = {0};
    static const char uri[] = "https://example.test/event";

    event.uri = malloc(sizeof(uri));
    assert(event.uri);
    memcpy(event.uri, uri, sizeof(uri));
    assert(player_event_copy(&copy, &event));
    assert(copy.uri != event.uri && strcmp(copy.uri, event.uri) == 0);
    assert(player_event_copy(&copy, &copy));
    assert(strcmp(copy.uri, "https://example.test/event") == 0);
    assert(player_event_copy(&copy, NULL));
    assert(copy.uri == NULL && copy.type == PLAYER_EVENT_STATE_CHANGED);
    player_event_clear(&copy);
    player_event_clear(&event);
}

static void test_snapshot_lifecycle(void)
{
    PlayerSnapshot snapshot = {
        .has_media = true,
        .state = PLAYER_STATE_PLAYING,
        .position_ms = 1234,
        .duration_ms = 5678,
        .volume = 80,
        .seekable = true,
    };
    PlayerSnapshot copy = {0};

    assert(player_media_set(&snapshot.media, "https://example.test/snapshot", "meta"));
    assert(player_snapshot_copy(&copy, &snapshot));
    assert(copy.has_media && copy.media.uri != snapshot.media.uri);
    assert(copy.position_ms == 1234 && copy.duration_ms == 5678);
    assert(player_snapshot_copy(&copy, &copy));
    assert(strcmp(copy.media.uri, "https://example.test/snapshot") == 0);
    player_snapshot_clear(&copy);
    player_snapshot_clear(&snapshot);
}

int main(void)
{
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        test_media_lifecycle();
        test_event_lifecycle();
        test_snapshot_lifecycle();
    }
    puts("player owned-value tests passed");
    return 0;
}
