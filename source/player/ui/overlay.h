#pragma once

#include <stdbool.h>

#include "player/types.h"

typedef enum
{
    PLAYER_UI_OVERLAY_NONE = 0,
    PLAYER_UI_OVERLAY_MESSAGE,
    PLAYER_UI_OVERLAY_BAR
} PlayerUiOverlayKind;

typedef enum
{
    PLAYER_UI_OVERLAY_FOCUS_NONE = 0,
    PLAYER_UI_OVERLAY_FOCUS_PLAY,
    PLAYER_UI_OVERLAY_FOCUS_PAUSE,
    PLAYER_UI_OVERLAY_FOCUS_SEEK,
    PLAYER_UI_OVERLAY_FOCUS_VOLUME,
    PLAYER_UI_OVERLAY_FOCUS_STATUS
} PlayerUiOverlayFocus;

typedef struct
{
    char title[64];
    char line1[128];
} PlayerUiOverlayMessage;

typedef struct
{
    char title[64];
    char subtitle[96];
    char left[48];
    char center[96];
    char right[48];
    char hint[96];
    int progress_permille;
    int position_ms;
    int duration_ms;
    int volume;
    bool mute;
    bool seekable;
    PlayerUiOverlayFocus focus;
    int seek_delta_ms;
    PlayerState state;
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
