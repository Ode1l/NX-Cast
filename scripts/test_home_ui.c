#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "player/ui/home.h"

int main(void)
{
    char directory[] = "/tmp/nxcast-home-XXXXXX";
    char path[256];
    assert(mkdtemp(directory));
    snprintf(path, sizeof(path), "%s/language.txt", directory);
    home_ui_init(path, true);
    assert(home_ui_is_chinese());
    assert(home_ui_toggle_language());
    assert(!home_ui_is_chinese());
    home_ui_init(path, true);
    assert(!home_ui_is_chinese());
    assert(home_ui_toggle_language());
    home_ui_init(path, false);
    assert(home_ui_is_chinese());
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("invalid\n", file);
    fclose(file);
    home_ui_init(path, false);
    assert(!home_ui_is_chinese());
    home_ui_init(NULL, false);
    assert(!home_ui_toggle_language());
    assert(home_ui_is_chinese());

    assert(home_ui_hit(HOME_FOCUS_TV, 940, 320));
    assert(home_ui_hit(HOME_FOCUS_LANGUAGE, 1100, 60));
    assert(!home_ui_hit(HOME_FOCUS_TV, 336, 320));
    assert(!home_ui_hit(HOME_FOCUS_LANGUAGE, 336, 320));
    assert(!home_ui_hit(HOME_FOCUS_TV, 940, 660));
    assert(!home_ui_hit(HOME_FOCUS_LANGUAGE, 1100, 100));
    assert(!home_ui_hit(HOME_FOCUS_TV, HOME_TV_RIGHT, 320));
    assert(!home_ui_hit((HomeFocus)99, 940, 320));
    remove(path);
    rmdir(directory);
    puts("Home language persistence and touch geometry passed");
    return 0;
}
