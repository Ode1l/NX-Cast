#pragma once

#include <stdbool.h>

static inline bool player_iptv_video_menu_available(bool iptv_playback_active,
                                                   int channel_count)
{
    /* Allow opening Sources even when the current catalog becomes empty. */
    (void)channel_count;
    return iptv_playback_active;
}
