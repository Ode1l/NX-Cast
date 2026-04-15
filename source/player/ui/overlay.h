#pragma once

#include <stdbool.h>

typedef enum
{
    PLAYER_UI_OVERLAY_NONE = 0,
    PLAYER_UI_OVERLAY_MESSAGE,
    PLAYER_UI_OVERLAY_BAR
} PlayerUiOverlayKind;

typedef struct
{
    char title[64];
    char line1[128];
} PlayerUiOverlayMessage;

typedef struct
{
    char title[64];
    char left[48];
    char center[96];
    char right[48];
    int progress_permille;
} PlayerUiOverlayBar;

typedef struct
{
    PlayerUiOverlayKind kind;
    int duration_ms;
    PlayerUiOverlayMessage message;
    PlayerUiOverlayBar bar;
} PlayerUiOverlaySnapshot;

int player_ui_overlay_show_message(const char *title, const char *line1, int duration_ms);
int player_ui_overlay_show_bar(const PlayerUiOverlayBar *bar, int duration_ms);
int player_ui_overlay_show_text(const char *text, int duration_ms);
bool player_ui_overlay_get_snapshot(PlayerUiOverlaySnapshot *out);
void player_ui_overlay_clear(void);
