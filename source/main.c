#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log/log.h"
#include "player/player.h"
#include "player/view.h"
#include "protocol/dlna_control.h"
#include "protocol/dlna/description/resource_store.h"
// #include "protocol/airplay/discovery/mdns.h"

typedef struct
{
    bool active;
    s32 last_y;
} TouchScrollState;

#define ANSI_RESET "\x1b[0m"
#define ANSI_DEBUG "\x1b[36;1m"
#define ANSI_INFO  "\x1b[32;1m"
#define ANSI_WARN  "\x1b[33;1m"
#define ANSI_ERROR "\x1b[31;1m"
#define ANSI_TEXT  "\x1b[37;1m"

static int g_nxlinkSock = -1;
static bool g_nxlinkWasActive = false;
static const char g_shutdownTraceDirParent[] = "sdmc:/switch";
static const char g_shutdownTraceDir[] = "sdmc:/switch/NX-Cast";
static const char g_shutdownTracePath[] = "sdmc:/switch/NX-Cast/shutdown_trace.log";

static void shutdown_stdio_trace(const char *fmt, ...);

typedef enum
{
    EXIT_REASON_UNKNOWN = 0,
    EXIT_REASON_PLUS_BUTTON,
    EXIT_REASON_APPLET_LOOP_ENDED
} ExitReason;

static void set_power_policy(bool active, bool use_logger)
{
    Result media_rc = appletSetMediaPlaybackState(active);
    Result sleep_rc = appletSetAutoSleepDisabled(active);

    if (use_logger)
    {
        if (R_FAILED(media_rc))
            log_warn("[power] appletSetMediaPlaybackState(%d) failed: 0x%x\n", active ? 1 : 0, media_rc);
        else
            log_info("[power] media playback state=%d\n", active ? 1 : 0);

        if (R_FAILED(sleep_rc))
            log_warn("[power] appletSetAutoSleepDisabled(%d) failed: 0x%x\n", active ? 1 : 0, sleep_rc);
        else
            log_info("[power] auto sleep disabled=%d\n", active ? 1 : 0);
    }
    else
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=power_policy media_playback=%d rc=0x%x auto_sleep_disabled=%d rc=0x%x\n",
                             active ? 1 : 0,
                             media_rc,
                             active ? 1 : 0,
                             sleep_rc);
    }
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static int wrapped_line_count(const char *line, int width)
{
    if (width <= 0)
        return 1;
    if (!line)
        return 1;

    size_t len = strlen(line);
    if (len == 0)
        return 1;

    return (int)((len + (size_t)width - 1) / (size_t)width);
}

static const char *color_for_log_line(const char *line)
{
    if (!line)
        return ANSI_TEXT;
    if (strncmp(line, "[ERROR]", 7) == 0)
        return ANSI_ERROR;
    if (strncmp(line, "[WARN]", 6) == 0)
        return ANSI_WARN;
    if (strncmp(line, "[INFO]", 6) == 0)
        return ANSI_INFO;
    if (strncmp(line, "[DEBUG]", 7) == 0)
        return ANSI_DEBUG;
    return ANSI_TEXT;
}

static const char *exit_reason_name(ExitReason reason)
{
    switch (reason)
    {
    case EXIT_REASON_PLUS_BUTTON:
        return "plus-button";
    case EXIT_REASON_APPLET_LOOP_ENDED:
        return "applet-loop-ended";
    case EXIT_REASON_UNKNOWN:
    default:
        return "unknown";
    }
}

static void shutdown_stdio_trace(const char *fmt, ...)
{
    va_list args;
    FILE *file = NULL;
    char line[512];
    int written = 0;

    if (!fmt)
        return;

    if (mkdir(g_shutdownTraceDirParent, 0777) != 0 && errno != EEXIST)
        return;
    if (mkdir(g_shutdownTraceDir, 0777) != 0 && errno != EEXIST)
        return;

    va_start(args, fmt);
    written = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (written <= 0)
        return;

    file = fopen(g_shutdownTracePath, "ab");
    if (file)
    {
        fwrite(line, 1, (size_t)written < sizeof(line) ? (size_t)written : strlen(line), file);
        fflush(file);
        fsync(fileno(file));
        fclose(file);
    }

}

static size_t compute_total_visual_lines(int width)
{
    size_t total_entries = log_history_count();
    size_t total_visual_lines = 0;
    char line[512];

    for (size_t i = 0; i < total_entries; ++i)
    {
        if (!log_history_get_line(i, line, sizeof(line)))
            continue;
        total_visual_lines += (size_t)wrapped_line_count(line, width);
    }

    return total_visual_lines;
}

static void render_log_view(int scroll_from_bottom)
{
    PrintConsole *con = consoleGetDefault();
    int width = con ? con->windowWidth : 80;
    int height = con ? con->windowHeight : 45;
    const int header_lines = 3;
    int visible_lines = height - header_lines;
    if (visible_lines < 1)
        visible_lines = 1;

    size_t total_entries = log_history_count();
    size_t total_visual_lines = compute_total_visual_lines(width);
    size_t max_scroll = total_visual_lines > (size_t)visible_lines ? total_visual_lines - (size_t)visible_lines : 0;
    int scroll = clamp_int(scroll_from_bottom, 0, (int)max_scroll);
    size_t start_visual = total_visual_lines > (size_t)visible_lines
                              ? total_visual_lines - (size_t)visible_lines - (size_t)scroll
                              : 0;

    consoleClear();
    printf("NX-Cast  (+ Exit)\n");
    printf("Logs %zu  Rows %zu  Scroll %d/%zu\n", total_entries, total_visual_lines, scroll, max_scroll);
    printf("Up/Down + Stick + Touch drag\n");

    size_t visual_cursor = 0;
    int printed_rows = 0;
    char line[512];
    for (size_t i = 0; i < total_entries && printed_rows < visible_lines; ++i)
    {
        if (!log_history_get_line(i, line, sizeof(line)))
            continue;

        int line_rows = wrapped_line_count(line, width);
        size_t line_start_visual = visual_cursor;
        size_t line_end_visual = visual_cursor + (size_t)line_rows;
        if (line_end_visual <= start_visual)
        {
            visual_cursor = line_end_visual;
            continue;
        }

        int row_start = 0;
        if (start_visual > line_start_visual)
            row_start = (int)(start_visual - line_start_visual);

        size_t line_len = strlen(line);
        const char *color = color_for_log_line(line);
        for (int row = row_start; row < line_rows && printed_rows < visible_lines; ++row)
        {
            size_t chunk_start = (size_t)row * (size_t)width;
            size_t chunk_len = 0;
            if (chunk_start < line_len)
            {
                chunk_len = line_len - chunk_start;
                if (chunk_len > (size_t)width)
                    chunk_len = (size_t)width;
            }

            printf("%s", color);
            if (chunk_len > 0)
                printf("%.*s", (int)chunk_len, line + chunk_start);
            printf(ANSI_RESET "\n");
            ++printed_rows;
        }

        visual_cursor = line_end_visual;
    }

    while (printed_rows < visible_lines)
    {
        printf("\n");
        ++printed_rows;
    }
}

static bool initialize_network(void)
{
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc))
    {
        log_error("[net] socketInitializeDefault() failed: 0x%08X\n", rc);
        log_warn("[net] Network features unavailable.\n");
        return false;
    }

    log_info("[net] Network stack initialized.\n");
    log_info("[net] Ensure Wi-Fi is connected before streaming.\n");
    return true;
}

static void enable_nxlink_stdio(bool network_ready)
{
    if (!network_ready)
        return;

    if (__nxlink_host.s_addr == 0)
    {
        log_set_stdio_mirror(false);
        log_warn("[log] nxlink host unavailable; local console only.\n");
        return;
    }

    g_nxlinkSock = nxlinkStdioForDebug();
    if (g_nxlinkSock >= 0)
    {
        g_nxlinkWasActive = true;
        log_set_stdio_mirror(true);
        log_info("[log] nxlink stderr connected host=%s port=%d fd=%d stdout=local\n",
                 inet_ntoa(__nxlink_host),
                 NXLINK_CLIENT_PORT,
                 g_nxlinkSock);
        return;
    }

    log_set_stdio_mirror(false);
    log_warn("[log] nxlink stderr connect failed host=%s port=%d\n",
             inet_ntoa(__nxlink_host),
             NXLINK_CLIENT_PORT);
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    
    if (!log_runtime_init())
    {
        printf("[ERROR] [log] log_runtime_init failed\n");
    }
    log_set_level(LOG_LEVEL_DEBUG);
    log_info("[log] level=DEBUG\n");
    set_power_policy(true, true);

    bool storageReady = dlna_resource_store_ensure_defaults();
    if (storageReady)
        log_info("[storage] DLNA resources ready on SD.\n");
    else
        log_warn("[storage] failed to prepare DLNA resources on SD.\n");

    bool networkReady = initialize_network();
    enable_nxlink_stdio(networkReady);
    bool dlnaRunning = false;
    bool videoPlatformReady = player_view_init();

    if (networkReady)
    {
        dlnaRunning = dlna_control_start();
        // mdns_discover_airplay();
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();

    PadState pad;
    padInitializeDefault(&pad);
    PadRepeater repeater;
    padRepeaterInitialize(&repeater, 14, 3);

    int scroll_from_bottom = 0;
    int stick_repeat_cooldown = 0;
    TouchScrollState touch_scroll = {0};
    ExitReason exit_reason = EXIT_REASON_UNKNOWN;

    log_info("[ui] NX-Cast starting. Press + to exit.\n");
    log_info("[ui] Use Up/Down, sticks, or touch drag to scroll logs.\n");
    if (videoPlatformReady)
        log_info("[ui] Video view auto-activates while playback is active.\n");

    while (appletMainLoop())
    {
        padUpdate(&pad);

        PlayerSnapshot snapshot;
        PlayerViewMode active_view = PLAYER_VIEW_LOG;
        if (videoPlatformReady)
        {
            if (player_get_snapshot(&snapshot))
            {
                player_view_sync(&snapshot);
                player_snapshot_clear(&snapshot);
            }
            player_view_begin_frame();
            active_view = player_view_get_mode();
        }

        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);

        if (kDown & HidNpadButton_Plus)
        {
            exit_reason = EXIT_REASON_PLUS_BUTTON;
            log_set_stdio_mirror(false);
            shutdown_stdio_trace("[INFO] [shutdown] requested reason=%s\n", exit_reason_name(exit_reason));
            break;
        }

        if (active_view == PLAYER_VIEW_LOG)
        {
            PrintConsole *con = consoleGetDefault();
            int width = con ? con->windowWidth : 80;
            int visible_lines = (con ? con->windowHeight : 45) - 3;
            if (visible_lines < 1)
                visible_lines = 1;
            size_t total_visual_lines = compute_total_visual_lines(width);
            int max_scroll = 0;
            if (total_visual_lines > (size_t)visible_lines)
                max_scroll = (int)(total_visual_lines - (size_t)visible_lines);

            padRepeaterUpdate(&repeater, kHeld & (HidNpadButton_Up | HidNpadButton_Down));
            u64 repeated = padRepeaterGetButtons(&repeater);
            u64 nav_buttons = kDown | repeated;
            if (nav_buttons & HidNpadButton_Up)
                scroll_from_bottom = clamp_int(scroll_from_bottom + 1, 0, max_scroll);
            if (nav_buttons & HidNpadButton_Down)
                scroll_from_bottom = clamp_int(scroll_from_bottom - 1, 0, max_scroll);

            HidAnalogStickState l = padGetStickPos(&pad, 0);
            HidAnalogStickState r = padGetStickPos(&pad, 1);
            int stick_y = l.y;
            if (abs(r.y) > abs(stick_y))
                stick_y = r.y;

            if (stick_repeat_cooldown > 0)
                --stick_repeat_cooldown;
            else if (stick_y > 12000)
            {
                int step = stick_y > 24000 ? 2 : 1;
                scroll_from_bottom = clamp_int(scroll_from_bottom + step, 0, max_scroll);
                stick_repeat_cooldown = 3;
            }
            else if (stick_y < -12000)
            {
                int step = stick_y < -24000 ? 2 : 1;
                scroll_from_bottom = clamp_int(scroll_from_bottom - step, 0, max_scroll);
                stick_repeat_cooldown = 3;
            }

            HidTouchScreenState touch_state;
            size_t touch_total = hidGetTouchScreenStates(&touch_state, 1);
            if (touch_total > 0 && touch_state.count > 0)
            {
                s32 y = (s32)touch_state.touches[0].y;
                if (!touch_scroll.active)
                {
                    touch_scroll.active = true;
                    touch_scroll.last_y = y;
                }
                else
                {
                    s32 dy = y - touch_scroll.last_y;
                    const int pixels_per_line = 18;
                    if (dy >= pixels_per_line || dy <= -pixels_per_line)
                    {
                        int step = dy / pixels_per_line;
                        scroll_from_bottom = clamp_int(scroll_from_bottom + step, 0, max_scroll);
                        touch_scroll.last_y = y;
                    }
                }
            }
            else
            {
                touch_scroll.active = false;
            }

            scroll_from_bottom = clamp_int(scroll_from_bottom, 0, max_scroll);
            render_log_view(scroll_from_bottom);
            consoleUpdate(NULL);
        }
        else
        {
            touch_scroll.active = false;
            player_view_render_frame();
        }
    }

    if (exit_reason == EXIT_REASON_UNKNOWN)
        exit_reason = EXIT_REASON_APPLET_LOOP_ENDED;

    log_set_stdio_mirror(false);
    shutdown_stdio_trace("[INFO] [shutdown] begin reason=%s network_ready=%d dlna_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
                         exit_reason_name(exit_reason),
                         networkReady ? 1 : 0,
                         dlnaRunning ? 1 : 0,
                         videoPlatformReady ? 1 : 0,
                         g_nxlinkSock,
                         storageReady ? 1 : 0);
    log_info("[shutdown] begin reason=%s network_ready=%d dlna_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
             exit_reason_name(exit_reason),
             networkReady ? 1 : 0,
             dlnaRunning ? 1 : 0,
             videoPlatformReady ? 1 : 0,
             g_nxlinkSock,
             storageReady ? 1 : 0);

    if (networkReady)
    {
        if (dlnaRunning)
        {
            shutdown_stdio_trace("[INFO] [shutdown] step=dlna_control_stop begin\n");
            log_info("[shutdown] step=dlna_control_stop begin\n");
            dlna_control_stop();
            shutdown_stdio_trace("[INFO] [shutdown] step=dlna_control_stop done\n");
            log_info("[shutdown] step=dlna_control_stop done\n");
        }
        else
        {
            log_info("[shutdown] step=dlna_control_stop skip reason=not-running\n");
        }
    }
    else
    {
        log_info("[shutdown] step=dlna_control_stop skip reason=network-disabled\n");
    }

    if (videoPlatformReady)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=player_view_deinit begin\n");
        log_info("[shutdown] step=player_view_deinit begin\n");
        player_view_deinit();
        shutdown_stdio_trace("[INFO] [shutdown] step=player_view_deinit done\n");
        log_info("[shutdown] step=player_view_deinit done\n");
    }
    else
    {
        log_info("[shutdown] step=player_view_deinit skip reason=not-initialized\n");
    }

    shutdown_stdio_trace("[INFO] [shutdown] step=log_runtime_shutdown begin\n");
    log_info("[shutdown] step=log_runtime_shutdown begin\n");
    log_runtime_shutdown();
    shutdown_stdio_trace("[INFO] [shutdown] step=log_runtime_shutdown done\n");
    set_power_policy(false, false);

    if (g_nxlinkSock >= 0)
    {
        int nxlink_fd = g_nxlinkSock;
        g_nxlinkSock = -1;
        shutdown_stdio_trace("[INFO] [shutdown] step=stderr_detach begin device=null\n");
        fflush(stdout);
        fflush(stderr);
        consoleDebugInit(debugDevice_NULL);
        shutdown_stdio_trace("[INFO] [shutdown] step=stderr_detach done\n");
        shutdown_stdio_trace("[INFO] [shutdown] step=nxlink_close begin fd=%d\n", nxlink_fd);
        close(nxlink_fd);
        shutdown_stdio_trace("[INFO] [shutdown] step=nxlink_close done\n");
    }
    else
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=nxlink_close skip reason=no-fd\n");
    }

    if (networkReady)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=socketExit begin\n");
        socketExit();
        shutdown_stdio_trace("[INFO] [shutdown] step=socketExit done\n");
    }

    if (g_nxlinkWasActive)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=consoleExit skip reason=nxlink-session\n");
    }
    else if (videoPlatformReady)
    {
        // Video platform was initialized, which calls consoleExit during frontend_open
        // and consoleInit during frontend_close. Skip the final consoleExit to avoid
        // interfering with deko3d resource cleanup
        shutdown_stdio_trace("[INFO] [shutdown] step=consoleExit skip reason=video-platform-handled-console\n");
    }
    else
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=consoleExit begin\n");
        consoleExit(NULL);
        shutdown_stdio_trace("[INFO] [shutdown] step=consoleExit done\n");
    }
    return 0;
}
