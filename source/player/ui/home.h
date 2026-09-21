#pragma once

#include <stdbool.h>

typedef enum {
    HOME_FOCUS_TV = 0,
    HOME_FOCUS_LANGUAGE
} HomeFocus;

/* Logical 1280x720 coordinates shared by rendering and touch input. */
enum {
    HOME_TV_LEFT = 656, HOME_TV_TOP = 124,
    HOME_TV_RIGHT = 1232, HOME_TV_BOTTOM = 596,
    HOME_LANGUAGE_LEFT = 1056, HOME_LANGUAGE_TOP = 32,
    HOME_LANGUAGE_RIGHT = 1232, HOME_LANGUAGE_BOTTOM = 88
};

void home_ui_init(const char *preference_path, bool system_chinese);
bool home_ui_is_chinese(void);
bool home_ui_toggle_language(void);
const char *home_ui_text(const char *english, const char *chinese);
bool home_ui_hit(HomeFocus target, int x, int y);
