#include "home.h"

#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
    if (!file && errno == ENOENT)
    {
        char backup[sizeof(g_preference_path) + 5];
        snprintf(backup, sizeof(backup), "%s.bak", g_preference_path);
        file = fopen(backup, "r");
    }
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

const char *home_ui_translate(const char *english)
{
    static const struct {
        const char *english;
        const char *chinese;
    } labels[] = {
        {"Play", "播放"},
        {"Pause", "暂停"},
        {"Play/Pause", "播放/暂停"},
        {"Home", "主页"},
        {"Channels", "频道"},
        {"Select", "选择"},
        {"Back", "返回"},
        {"Favorite", "收藏"},
        {"Full list", "完整列表"},
        {"Collapse", "收起"},
        {"Browse", "浏览"},
        {"Move", "移动"},
        {"Manage sources", "管理源"},
        {"Close", "关闭"},
        {"Volume", "音量"},
        {"Seek", "跳转"},
        {"Muted", "已静音"},
        {"Loading", "加载中"},
        {"Buffering", "缓冲中"},
        {"buffering", "缓冲中"},
        {"Seeking", "跳转中"},
        {"Paused", "已暂停"},
        {"Playing", "播放中"},
        {"Error", "错误"},
        {"Stopped", "已停止"},
        {"Ready", "就绪"},
        {"PLAY", "播放"},
        {"PAUSE", "暂停"},
        {"HOME", "主页"},
        {"CHANNELS", "频道"},
        {"VOLUME", "音量"},
        {"SEEK", "跳转"},
        {"LOADING", "加载中"},
        {"BUFFERING", "缓冲中"},
        {"SEEKING", "跳转中"},
        {"PAUSED", "已暂停"},
        {"PLAYING", "播放中"},
        {"ERROR", "错误"},
        {"STOPPED", "已停止"},
        {"READY", "就绪"},
        {"PREPARING STREAM", "正在准备媒体流"},
        {"WAITING FOR DATA", "正在等待数据"},
        {"MOVING PLAYHEAD", "正在跳转播放位置"},
        {"PLAYBACK ERROR", "播放错误"},
        {"CHECK STREAM", "请检查媒体流"},
        {"Play/Pause failed", "播放/暂停失败"},
        {"Seek unavailable", "无法跳转"},
        {"Seek failed", "跳转失败"},
        {"Volume failed", "音量调整失败"},
        {"A PLAY", "A 播放"},
        {"A PAUSE", "A 暂停"},
        {"Volume %d%%", "音量 %d%%"},
        {"A PLAY  L/R SEEK  UP/DN VOL  B HOME  X TV", "A 播放  L/R 跳转  上/下 音量  B 主页  X 电视"},
        {"A PAUSE  L/R SEEK  UP/DN VOL  B HOME  X TV", "A 暂停  L/R 跳转  上/下 音量  B 主页  X 电视"},
    };

    if (!english || !home_ui_is_chinese())
        return english;
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
    {
        if (strcmp(english, labels[i].english) == 0)
            return labels[i].chinese;
    }
    return english;
}

bool home_ui_toggle_language(void)
{
    char temporary[sizeof(g_preference_path) + 5];
    bool chinese = !home_ui_is_chinese();
    /* Apply immediately even if SD storage is unavailable. */
    atomic_store(&g_chinese, chinese);
    if (!g_preference_path[0])
    {
        errno = EINVAL;
        return false;
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp", g_preference_path);
    FILE *file = fopen(temporary, "w");
    if (!file)
        return false;
    bool ok = fputs(chinese ? "zh-CN\n" : "en\n", file) >= 0;
    int error = ok ? 0 : errno;
    if (fclose(file) != 0)
    {
        ok = false;
        error = errno;
    }
    if (ok)
    {
        /* SD rename need not replace an existing destination. Keep a recovery
         * file until the fully written new preference has been installed. */
        char backup[sizeof(g_preference_path) + 5];
        struct stat st;
        snprintf(backup, sizeof(backup), "%s.bak", g_preference_path);
        bool existed = stat(g_preference_path, &st) == 0;
        if (existed && !S_ISREG(st.st_mode))
        {
            ok = false;
            error = EISDIR;
        }
        else if (!existed && errno != ENOENT)
        {
            ok = false;
            error = errno;
        }
        else if (existed && remove(backup) != 0 && errno != ENOENT)
        {
            ok = false;
            error = errno;
        }
        else if (existed && rename(g_preference_path, backup) != 0)
        {
            ok = false;
            error = errno;
        }
        else if (rename(temporary, g_preference_path) != 0)
        {
            ok = false;
            error = errno;
            if (existed)
                rename(backup, g_preference_path);
        }
        else
            remove(backup);
    }
    if (!ok)
    {
        remove(temporary);
        errno = error;
    }
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
