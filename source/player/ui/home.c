#include "home.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_bool g_chinese;
static char g_preference_path[512];

void home_ui_init(const char *preference_path, bool system_chinese)
{
    char value[16];
    atomic_store(&g_chinese, system_chinese);
    g_preference_path[0] = '\0';
    if (!preference_path || strlen(preference_path) >= sizeof(g_preference_path))
        return;
    strcpy(g_preference_path, preference_path);
    FILE *file = fopen(g_preference_path, "r");
    if (!file)
        return;
    if (fgets(value, sizeof(value), file))
    {
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(value, "zh-CN") == 0)
            atomic_store(&g_chinese, true);
        else if (strcmp(value, "en") == 0)
            atomic_store(&g_chinese, false);
    }
    fclose(file);
}

bool home_ui_is_chinese(void)
{
    return atomic_load(&g_chinese);
}

const char *home_ui_text(const char *english, const char *chinese)
{
    return home_ui_is_chinese() ? chinese : english;
}

bool home_ui_toggle_language(void)
{
    char temporary[sizeof(g_preference_path) + 5];
    bool chinese = !home_ui_is_chinese();
    /* Apply immediately even if SD storage is unavailable. */
    atomic_store(&g_chinese, chinese);
    if (!g_preference_path[0])
        return false;
    snprintf(temporary, sizeof(temporary), "%s.tmp", g_preference_path);
    FILE *file = fopen(temporary, "w");
    if (!file)
        return false;
    bool ok = fputs(chinese ? "zh-CN\n" : "en\n", file) >= 0;
    if (fclose(file) != 0)
        ok = false;
    if (ok)
        ok = rename(temporary, g_preference_path) == 0;
    if (!ok)
        remove(temporary);
    return ok;
}

bool home_ui_hit(HomeFocus target, int x, int y)
{
    if (target == HOME_FOCUS_LANGUAGE)
        return x >= HOME_LANGUAGE_LEFT && x < HOME_LANGUAGE_RIGHT &&
               y >= HOME_LANGUAGE_TOP && y < HOME_LANGUAGE_BOTTOM;
    if (target == HOME_FOCUS_TV)
        return x >= HOME_TV_LEFT && x < HOME_TV_RIGHT &&
               y >= HOME_TV_TOP && y < HOME_TV_BOTTOM;
    return false;
}
