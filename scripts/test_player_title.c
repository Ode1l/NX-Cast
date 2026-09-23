#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player/ui/bar.h"

int player_ui_overlay_show_message(const char *title, const char *line, int duration)
{ (void)title; (void)line; (void)duration; return 0; }
int player_ui_overlay_show_bar(const PlayerUiOverlayBar *bar, int duration)
{ (void)bar; (void)duration; return 0; }

int main(void)
{
    PlayerSnapshot snapshot = {0};
    PlayerUiOverlayBar bar;
    char title[601], xml[700], oversized[1201];
    memset(title, 'A', sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;
    snapshot.has_media = true;
    snapshot.media.metadata = title;
    player_ui_bar_build(&snapshot, NULL, &bar);
    assert(strcmp(bar.subtitle, title) == 0);
    snprintf(xml, sizeof(xml), "<dc:title>%s</dc:title>", title);
    snapshot.media.metadata = xml;
    player_ui_bar_build(&snapshot, NULL, &bar);
    assert(strcmp(bar.subtitle, title) == 0);
    for (size_t i = 0; i < 1200; i += 3)
        memcpy(oversized + i, "\xe4\xb8\xad", 3);
    oversized[1200] = 0;
    snapshot.media.metadata = oversized;
    player_ui_bar_build(&snapshot, NULL, &bar);
    assert(strlen(bar.subtitle) == 1023);
    assert(strlen(bar.subtitle) % 3 == 0);
    puts("Long player title preservation and UTF-8 boundary tests passed");
    return 0;
}
