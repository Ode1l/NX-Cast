#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "player/ui/home.h"

#undef rename
extern int rename(const char *, const char *);
static bool fail_install;
int home_test_rename(const char *from, const char *to)
{
    /* Model SD rename: destination must not exist. */
    if (access(to, F_OK) == 0)
    {
        errno = EEXIST;
        return -1;
    }
    if (fail_install && strstr(from, ".tmp"))
    {
        errno = EIO;
        return -1;
    }
    return rename(from, to);
}

static void test_translation(void)
{
    static const char *labels[] = {
        "Play", "Pause", "Play/Pause", "Home", "Channels", "Volume", "Seek", "Muted",
        "Select", "Back", "Favorite", "Full list", "Collapse", "Browse", "Move", "Manage sources", "Close",
        "Loading", "Buffering", "buffering", "Seeking", "Paused", "Playing", "Error", "Stopped", "Ready",
        "PLAY", "PAUSE", "HOME", "CHANNELS", "VOLUME", "SEEK", "LOADING", "BUFFERING", "SEEKING",
        "PAUSED", "PLAYING", "ERROR", "STOPPED", "READY", "PREPARING STREAM", "WAITING FOR DATA",
        "MOVING PLAYHEAD", "PLAYBACK ERROR", "CHECK STREAM", "Play/Pause failed", "Seek unavailable",
        "Seek failed", "Volume failed", "A PLAY", "A PAUSE", "Volume %d%%",
        "A PLAY  L/R SEEK  UP/DN VOL  B HOME  X TV",
        "A PAUSE  L/R SEEK  UP/DN VOL  B HOME  X TV",
    };
    char unknown[] = "Example channel / Source 1";
    char seek[] = "SEEK +10S";
    char chinese_play[32];
    char volume[48];

    home_ui_init(NULL, false);
    assert(strcmp(home_ui_text("English", "Chinese"), "English") == 0);
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
        assert(home_ui_translate(labels[i]) == labels[i]);

    assert(!home_ui_toggle_language());
    assert(strcmp(home_ui_text("English", "Chinese"), "Chinese") == 0);
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
    {
        const char *translated = home_ui_translate(labels[i]);
        assert(translated && translated[0]);
        assert(strcmp(translated, labels[i]) != 0);
    }
    assert(home_ui_translate(NULL) == NULL);
    assert(strcmp(home_ui_translate(""), "") == 0);
    assert(home_ui_translate(unknown) == unknown);
    assert(home_ui_translate(seek) == seek);
    assert(strcmp(home_ui_translate("Play HD"), "Play HD") == 0);
    snprintf(chinese_play, sizeof(chinese_play), "%s", home_ui_translate("Play"));
    assert(home_ui_translate(chinese_play) == chinese_play);
    snprintf(volume, sizeof(volume), home_ui_translate("Volume %d%%"), 75);
    assert(strstr(volume, "75%"));
    assert(strlen(home_ui_translate(labels[sizeof(labels) / sizeof(labels[0]) - 1])) < 96);

    assert(!home_ui_toggle_language());
    assert(strcmp(home_ui_translate("Play"), "Play") == 0);
    assert(strcmp(chinese_play, "Play") != 0);
}

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
    fail_install = true;
    assert(!home_ui_toggle_language());
    assert(errno == EIO);
    fail_install = false;
    home_ui_init(path, false);
    assert(home_ui_is_chinese()); /* Failed install restored previous file. */
    char backup[272];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    assert(rename(path, backup) == 0);
    home_ui_init(path, false);
    assert(home_ui_is_chinese()); /* Interrupted replacement is recoverable. */
    assert(home_ui_toggle_language());
    assert(access(backup, F_OK) != 0);
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("invalid\n", file);
    fclose(file);
    home_ui_init(path, false);
    assert(!home_ui_is_chinese());
    home_ui_init(path, true);
    assert(home_ui_is_chinese());
    file = fopen(path, "w");
    assert(file);
    fputs("en\r\n", file);
    fclose(file);
    home_ui_init(path, true);
    assert(!home_ui_is_chinese());
    home_ui_init(directory, false);
    assert(!home_ui_toggle_language());
    assert(home_ui_is_chinese());
    char temporary[272];
    snprintf(temporary, sizeof(temporary), "%s.tmp", directory);
    assert(access(temporary, F_OK) != 0);
    home_ui_init(NULL, false);
    assert(!home_ui_toggle_language());
    assert(home_ui_is_chinese());
    test_translation();

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
    puts("Home language persistence, built-in translations and touch geometry passed");
    return 0;
}
