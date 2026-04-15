#include "player/ui/timeline.h"

#include <stdio.h>

#define PLAYER_UI_PROGRESS_CELLS 18

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void format_media_time(int position_ms, char *out, size_t out_size)
{
    int total_seconds;
    int hour;
    int minute;
    int second;

    if (!out || out_size == 0)
        return;

    if (position_ms < 0)
        position_ms = 0;

    total_seconds = position_ms / 1000;
    hour = total_seconds / 3600;
    minute = (total_seconds / 60) % 60;
    second = total_seconds % 60;

    if (hour > 0)
        snprintf(out, out_size, "%02d:%02d:%02d", hour, minute, second);
    else
        snprintf(out, out_size, "%02d:%02d", minute, second);
}

static void build_progress_bar(int position_ms, int duration_ms, char *out, size_t out_size)
{
    int filled = 0;
    int i = 0;
    char *cursor = out;
    size_t remaining = out_size;

    if (!out || out_size == 0)
        return;

    if (duration_ms > 0)
    {
        position_ms = clamp_int(position_ms, 0, duration_ms);
        filled = (int)((((long long)position_ms * PLAYER_UI_PROGRESS_CELLS) + (duration_ms / 2)) / duration_ms);
        filled = clamp_int(filled, 0, PLAYER_UI_PROGRESS_CELLS);
    }

    if (remaining > 1)
    {
        *cursor++ = '[';
        --remaining;
    }

    for (i = 0; i < PLAYER_UI_PROGRESS_CELLS && remaining > 1; ++i)
    {
        *cursor++ = i < filled ? '=' : '-';
        --remaining;
    }

    if (remaining > 1)
    {
        *cursor++ = ']';
        --remaining;
    }

    *cursor = '\0';
}

void player_ui_timeline_describe(const PlayerSnapshot *snapshot, char *out, size_t out_size)
{
    char position_text[16];
    char duration_text[16];
    char progress_bar[PLAYER_UI_PROGRESS_CELLS + 3];

    if (!out || out_size == 0)
        return;

    out[0] = '\0';

    if (!snapshot || !snapshot->has_media)
        return;

    format_media_time(snapshot->position_ms, position_text, sizeof(position_text));

    if (snapshot->duration_ms > 0)
    {
        format_media_time(snapshot->duration_ms, duration_text, sizeof(duration_text));
        build_progress_bar(snapshot->position_ms, snapshot->duration_ms, progress_bar, sizeof(progress_bar));
        snprintf(out, out_size, "%s  %s / %s", progress_bar, position_text, duration_text);
        return;
    }

    snprintf(out, out_size, "%s", position_text);
}
