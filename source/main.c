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
#include "player/ui/layout.h"
#include "player/ui/ui.h"
#include "player/view.h"
#include "protocol/dlna_control.h"
#include "protocol/dlna/description/resource_store.h"
// #include "protocol/airplay/discovery/mdns.h"

typedef struct
{
    bool active;
    PlayerViewMode start_view;
    s32 start_x;
    s32 start_y;
    s32 last_x;
    s32 last_y;
    uint64_t start_ms;
} TouchTraceState;

typedef struct
{
    bool active;
    int target_ms;
    int last_preview_ms;
} TouchSeekState;

#define ANSI_RESET "\x1b[0m"
#define ANSI_ACCENT "\x1b[36;1m"
#define ANSI_ERROR "\x1b[31;1m"
#define ANSI_TEXT  "\x1b[37;1m"
#define ANSI_DIM   "\x1b[2m"

#define TOUCH_TAP_MAX_DURATION_MS 650ULL
#define TOUCH_TAP_MAX_MOVE_PX 24
#define TOUCH_TAP_DEBOUNCE_MS 350ULL

static int g_nxlinkSock = -1;
static bool g_nxlinkWasActive = false;
static bool g_networkInitialized = false;
static bool g_consoleInitialized = false;
static const char g_shutdownTraceDirParent[] = "sdmc:/switch";
static const char g_shutdownTraceDir[] = "sdmc:/switch/NX-Cast";
static const char g_shutdownTracePath[] = "sdmc:/switch/NX-Cast/shutdown_trace.log";

static void shutdown_stdio_trace(const char *fmt, ...);
static void shutdown_trace_reset(void);

typedef enum
{
    EXIT_REASON_UNKNOWN = 0,
    EXIT_REASON_PLUS_BUTTON,
    EXIT_REASON_APPLET_LOOP_ENDED
} ExitReason;

static uint64_t main_monotonic_time_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static const char *main_player_state_name(PlayerState state)
{
    switch (state)
    {
    case PLAYER_STATE_IDLE:
        return "idle";
    case PLAYER_STATE_STOPPED:
        return "stopped";
    case PLAYER_STATE_LOADING:
        return "loading";
    case PLAYER_STATE_BUFFERING:
        return "buffering";
    case PLAYER_STATE_SEEKING:
        return "seeking";
    case PLAYER_STATE_PLAYING:
        return "playing";
    case PLAYER_STATE_PAUSED:
        return "paused";
    case PLAYER_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static LogLevel main_input_trace_level(void)
{
#if defined(NXCAST_INPUT_TRACE_VERBOSE) && NXCAST_INPUT_TRACE_VERBOSE
    return LOG_LEVEL_WARN;
#else
    return LOG_LEVEL_INFO;
#endif
}

static void main_input_trace(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vlog_write(main_input_trace_level(), fmt, args);
    va_end(args);
}

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

static bool main_get_video_layout(PlayerUiLayout *out, int *out_width, int *out_height)
{
    PlayerUiLayout layout;
    PlayerViewStatus status = {0};
    int width = 1280;
    int height = 720;
    bool have_status;
    bool ok;

    have_status = player_view_get_status(&status);
    if (have_status)
    {
        if (status.display_width > 0)
            width = (int)status.display_width;
        if (status.display_height > 0)
            height = (int)status.display_height;
        player_view_status_clear(&status);
    }

    ok = player_ui_layout_compute(width, height, out ? out : &layout);
    if (out_width)
        *out_width = width;
    if (out_height)
        *out_height = height;
    return ok;
}

static bool main_touch_progress_target_ms(int x, int y, const PlayerSnapshot *snapshot, int *out_target_ms)
{
    PlayerUiLayout layout;

    if (!snapshot || !snapshot->has_media || !snapshot->seekable || snapshot->duration_ms <= 0 || !out_target_ms)
        return false;
    if (!main_get_video_layout(&layout, NULL, NULL))
        return false;
    if (!player_ui_layout_progress_hit_test(&layout, x, y))
        return false;

    *out_target_ms = player_ui_layout_progress_target_ms(&layout, x, snapshot->duration_ms);
    return true;
}

static bool main_touch_progress_drag_target_ms(int x, const PlayerSnapshot *snapshot, int *out_target_ms)
{
    PlayerUiLayout layout;

    if (!snapshot || !snapshot->has_media || !snapshot->seekable || snapshot->duration_ms <= 0 || !out_target_ms)
        return false;
    if (!main_get_video_layout(&layout, NULL, NULL))
        return false;

    *out_target_ms = player_ui_layout_progress_target_ms(&layout, x, snapshot->duration_ms);
    return true;
}

static bool main_touch_center_button_hit(int x, int y)
{
    int width;
    int height;
    int hit_size;

    if (!main_get_video_layout(NULL, &width, &height))
        return false;

    hit_size = clamp_int(height / 3, 180, 280);
    return x >= width / 2 - hit_size / 2 &&
           x <= width / 2 + hit_size / 2 &&
           y >= height / 2 - hit_size / 2 &&
           y <= height / 2 + hit_size / 2;
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

static void shutdown_trace_reset(void)
{
    FILE *file;

    if (mkdir(g_shutdownTraceDirParent, 0777) != 0 && errno != EEXIST)
        return;
    if (mkdir(g_shutdownTraceDir, 0777) != 0 && errno != EEXIST)
        return;

    file = fopen(g_shutdownTracePath, "wb");
    if (!file)
        return;
    fclose(file);
}

static bool get_latest_error_line(char *out, size_t out_size)
{
    char line[512];
    size_t count;

    if (!out || out_size == 0)
        return false;

    out[0] = '\0';
    count = log_history_count();
    while (count > 0)
    {
        --count;
        if (!log_history_get_line(count, line, sizeof(line)))
            continue;
        if (strncmp(line, "[ERROR]", 7) == 0)
        {
            snprintf(out, out_size, "%s", line);
            return true;
        }
    }

    return false;
}

static const char *ready_label(bool ready)
{
    return ready ? "OK" : "NOT READY";
}

static void build_home_view_state(PlayerHomeViewState *out,
                                  bool storage_ready,
                                  bool network_ready,
                                  bool dlna_running,
                                  bool video_ready)
{
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->storage_ready = storage_ready;
    out->network_ready = network_ready;
    out->dlna_running = dlna_running;
    out->video_ready = video_ready;
    out->has_error = get_latest_error_line(out->error_line, sizeof(out->error_line));
}

static void render_home_view(const PlayerHomeViewState *state)
{
    if (!state)
        return;

    consoleClear();
    printf("\n");
    printf(ANSI_ACCENT "  NX-CAST / HOME" ANSI_RESET "                                  " ANSI_TEXT "DLNA RECEIVER" ANSI_RESET "\n");
    printf(ANSI_ACCENT "  =======================================================================" ANSI_RESET "\n\n");

    printf(ANSI_TEXT "       CAST MEDIA TO YOUR SWITCH" ANSI_RESET "\n");
    printf("       Keep your phone and Switch on the same Wi-Fi, then select "
           ANSI_ACCENT "NX-Cast" ANSI_RESET ".\n\n");

    printf("  +-------------------+        +---------------+        +-------------------+\n");
    printf("  |  PHONE / DESKTOP  |        |     DLNA      |        |  NX-CAST SWITCH   |\n");
    printf("  |                   |        |               |        |                   |\n");
    printf("  |  Open video app   |  --->  |  Cast / Push  |  --->  |  Receive stream   |\n");
    printf("  |  iQiyi CCTV IPTV  |        |  one URL once |        |  Play on Switch   |\n");
    printf("  +-------------------+        +---------------+        +-------------------+\n\n");

    printf(ANSI_ACCENT "  [ QUICK START ]" ANSI_RESET "\n");
    printf("  1. Open a video app or IPTV page on your phone / desktop.\n");
    printf("  2. Tap Cast, DLNA, or media renderer, then choose " ANSI_ACCENT "NX-Cast" ANSI_RESET ".\n");
    printf("  3. Wait for the loading spinner; playback controls appear on touch.\n\n");

    printf(ANSI_ACCENT "  [ PLAYER CONTROLS ]" ANSI_RESET "\n");
    printf("  A Play/Pause     L/R Seek 10s     Up/Down Volume     + Exit\n");
    printf("  Touch: show/hide UI. Drag timeline: preview, release to seek.\n\n");

    printf(ANSI_ACCENT "  [ STATUS ]" ANSI_RESET " ");
    printf("Storage:%s%s%s  ",
           state->storage_ready ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->storage_ready),
           ANSI_RESET);
    printf("Network:%s%s%s  ",
           state->network_ready ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->network_ready),
           ANSI_RESET);
    printf("DLNA:%s%s%s  ",
           state->dlna_running ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->dlna_running),
           ANSI_RESET);
    printf("Player:%s%s%s\n\n",
           state->video_ready ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->video_ready),
           ANSI_RESET);

    printf(ANSI_ACCENT "  [ DIAGNOSTICS ]" ANSI_RESET "\n");
    if (state->has_error)
    {
        printf("  Latest error: " ANSI_ERROR "%s" ANSI_RESET "\n", state->error_line);
    }
    else
    {
        printf("  " ANSI_DIM "No error recorded. Full log is available through nxlink / in-memory history." ANSI_RESET "\n");
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

void userAppExit(void)
{
    shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit begin network_initialized=%d nxlink_fd=%d console_initialized=%d\n",
                         g_networkInitialized ? 1 : 0,
                         g_nxlinkSock,
                         g_consoleInitialized ? 1 : 0);

    if (g_nxlinkSock >= 0)
    {
        int nxlink_fd = g_nxlinkSock;
        g_nxlinkSock = -1;
        fflush(stdout);
        fflush(stderr);
        consoleDebugInit(debugDevice_NULL);
        shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit nxlink_close begin fd=%d\n", nxlink_fd);
        close(nxlink_fd);
        shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit nxlink_close done\n");
    }

    if (g_networkInitialized)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit socketExit begin\n");
        socketExit();
        g_networkInitialized = false;
        shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit socketExit done\n");
    }

    if (g_consoleInitialized)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=userAppExit consoleExit begin\n");
        consoleExit(NULL);
        g_consoleInitialized = false;
    }
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    g_consoleInitialized = true;
    shutdown_trace_reset();
    shutdown_stdio_trace("[INFO] [shutdown] trace_build=exit-userAppExit-v1\n");
    
    if (!log_runtime_init())
    {
        printf("[ERROR] [log] log_runtime_init failed\n");
    }
    set_power_policy(true, true);

    bool storageReady = dlna_resource_store_ensure_defaults();
    if (storageReady)
        log_info("[storage] DLNA resources ready on SD.\n");
    else
        log_warn("[storage] failed to prepare DLNA resources on SD.\n");

    bool networkReady = initialize_network();
    g_networkInitialized = networkReady;
    enable_nxlink_stdio(networkReady);
    bool dlnaRunning = false;
    bool videoPlatformReady = player_view_init();
    bool rendererPrestarted = false;
    bool videoRenderReady = false;

    if (networkReady)
    {
        if (videoPlatformReady)
        {
            rendererPrestarted = player_init();
            if (rendererPrestarted)
            {
                videoRenderReady = player_view_prepare_video();
                log_info("[ui] video render prepare=%d before_dlna=1\n", videoRenderReady ? 1 : 0);
            }
            else
            {
                log_warn("[ui] renderer init failed before DLNA start; SOAP actions may fail.\n");
            }
        }
        dlnaRunning = dlna_control_start();
        // mdns_discover_airplay();
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();

    PadState pad;
    padInitializeDefault(&pad);
    PadRepeater video_repeater;
    padRepeaterInitialize(&video_repeater, 20, 15);

    int video_stick_repeat_cooldown = 0;
    TouchTraceState touch_trace = {0};
    TouchSeekState touch_seek = {0};
    uint64_t last_touch_tap_ms = 0;
    PlayerViewMode last_logged_view = PLAYER_VIEW_HOME;
    bool have_logged_view = false;
    ExitReason exit_reason = EXIT_REASON_UNKNOWN;
    PlayerUiState video_ui;
    player_ui_reset(&video_ui);

    log_info("[ui] NX-Cast starting. Press + to exit.\n");
    log_info("[ui] Home view shows cast instructions and last error only.\n");
    if (videoPlatformReady)
        log_info("[ui] Video view auto-activates while playback is active.\n");

    while (appletMainLoop())
    {
        PlayerSnapshot snapshot = {0};
        PlayerHomeViewState home_state;
        bool have_snapshot = false;

        padUpdate(&pad);
        build_home_view_state(&home_state, storageReady, networkReady, dlnaRunning, videoRenderReady);

        PlayerViewMode active_view = PLAYER_VIEW_HOME;
        if (videoPlatformReady)
        {
            player_view_set_home_state(&home_state);
            if (player_get_snapshot(&snapshot))
            {
                have_snapshot = true;
                player_view_sync(&snapshot);
            }
            player_view_begin_frame();
            active_view = player_view_get_mode();
        }

        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        HidTouchScreenState touch_state = {0};
        size_t touch_total = hidGetTouchScreenStates(&touch_state, 1);
        bool touch_present = touch_total > 0 && touch_state.count > 0;
        bool touch_tap = false;
        s32 touch_x = 0;
        s32 touch_y = 0;
        s32 touch_tap_x = 0;
        s32 touch_tap_y = 0;
        uint64_t input_now_ms = main_monotonic_time_ms();

        if (!have_logged_view || active_view != last_logged_view)
        {
            main_input_trace("[input] active_view=%s state=%s media=%d snapshot=%d\n",
                             player_view_mode_name(active_view),
                             have_snapshot ? main_player_state_name(snapshot.state) : "none",
                             have_snapshot && snapshot.has_media ? 1 : 0,
                             have_snapshot ? 1 : 0);
            last_logged_view = active_view;
            have_logged_view = true;
        }

        if (touch_present)
        {
            touch_x = (s32)touch_state.touches[0].x;
            touch_y = (s32)touch_state.touches[0].y;
            if (!touch_trace.active)
            {
                touch_trace.active = true;
                touch_trace.start_x = touch_x;
                touch_trace.start_y = touch_y;
                touch_trace.start_ms = input_now_ms;
                touch_trace.start_view = active_view;
                main_input_trace("[input] touch begin view=%s x=%d y=%d state=%s media=%d\n",
                                 player_view_mode_name(active_view),
                                 (int)touch_x,
                                 (int)touch_y,
                                 have_snapshot ? main_player_state_name(snapshot.state) : "none",
                                 have_snapshot && snapshot.has_media ? 1 : 0);

                touch_seek.active = false;
                if (active_view == PLAYER_VIEW_VIDEO &&
                    have_snapshot &&
                    player_ui_overlay_visible(&video_ui) &&
                    main_touch_progress_target_ms((int)touch_x, (int)touch_y, &snapshot, &touch_seek.target_ms))
                {
                    touch_seek.active = true;
                    touch_seek.last_preview_ms = -1;
                    main_input_trace("[input] action=touch-seek begin target_ms=%d x=%d y=%d\n",
                                     touch_seek.target_ms,
                                     (int)touch_x,
                                     (int)touch_y);
                }
            }
            touch_trace.last_x = touch_x;
            touch_trace.last_y = touch_y;

            if (touch_seek.active &&
                have_snapshot &&
                main_touch_progress_drag_target_ms((int)touch_x, &snapshot, &touch_seek.target_ms) &&
                (touch_seek.last_preview_ms < 0 || abs(touch_seek.target_ms - touch_seek.last_preview_ms) >= 250))
            {
                bool ok = player_ui_preview_seek_to(&video_ui, &snapshot, touch_seek.target_ms);
                touch_seek.last_preview_ms = touch_seek.target_ms;
                main_input_trace("[input] action=touch-seek preview ok=%d target_ms=%d x=%d\n",
                                 ok ? 1 : 0,
                                 touch_seek.target_ms,
                                 (int)touch_x);
            }
        }
        else if (touch_trace.active)
        {
            uint64_t touch_duration_ms = input_now_ms - touch_trace.start_ms;
            int touch_dx = (int)(touch_trace.last_x - touch_trace.start_x);
            int touch_dy = (int)(touch_trace.last_y - touch_trace.start_y);
            bool touch_seek_released = false;
            bool tap_shape = touch_duration_ms <= TOUCH_TAP_MAX_DURATION_MS &&
                             abs(touch_dx) <= TOUCH_TAP_MAX_MOVE_PX &&
                             abs(touch_dy) <= TOUCH_TAP_MAX_MOVE_PX;
            bool tap_view = touch_trace.start_view == PLAYER_VIEW_VIDEO &&
                            active_view == PLAYER_VIEW_VIDEO;
            bool tap_debounced = last_touch_tap_ms != 0 &&
                                 input_now_ms - last_touch_tap_ms < TOUCH_TAP_DEBOUNCE_MS;

            main_input_trace("[input] touch end start_view=%s view=%s duration_ms=%llu dx=%d dy=%d state=%s media=%d\n",
                             player_view_mode_name(touch_trace.start_view),
                             player_view_mode_name(active_view),
                             (unsigned long long)touch_duration_ms,
                             touch_dx,
                             touch_dy,
                             have_snapshot ? main_player_state_name(snapshot.state) : "none",
                             have_snapshot && snapshot.has_media ? 1 : 0);

            if (touch_seek.active)
            {
                bool ok = have_snapshot && player_ui_seek_to(&video_ui, &snapshot, touch_seek.target_ms);
                main_input_trace("[input] action=touch-seek commit ok=%d target_ms=%d\n",
                                 ok ? 1 : 0,
                                 touch_seek.target_ms);
                touch_seek.active = false;
                touch_seek_released = true;
            }

            if (!touch_seek_released && tap_shape && tap_view && !tap_debounced)
            {
                touch_tap = true;
                touch_tap_x = touch_trace.last_x;
                touch_tap_y = touch_trace.last_y;
                last_touch_tap_ms = input_now_ms;
                main_input_trace("[input] touch tap accepted x=%d y=%d duration_ms=%llu\n",
                                 (int)touch_tap_x,
                                 (int)touch_tap_y,
                                 (unsigned long long)touch_duration_ms);
            }
            else if (!touch_seek_released)
            {
                main_input_trace("[input] touch tap skipped shape=%d view=%d debounce=%d\n",
                                 tap_shape ? 1 : 0,
                                 tap_view ? 1 : 0,
                                 tap_debounced ? 1 : 0);
            }
            touch_trace.active = false;
        }

        if (kDown)
        {
            main_input_trace("[input] buttons_down=0x%llx view=%s state=%s media=%d\n",
                             (unsigned long long)kDown,
                             player_view_mode_name(active_view),
                             have_snapshot ? main_player_state_name(snapshot.state) : "none",
                             have_snapshot && snapshot.has_media ? 1 : 0);
        }

        if (kDown & HidNpadButton_Plus)
        {
            exit_reason = EXIT_REASON_PLUS_BUTTON;
            log_set_stdio_mirror(false);
            shutdown_stdio_trace("[INFO] [shutdown] requested reason=%s\n", exit_reason_name(exit_reason));
            break;
        }

        if (active_view == PLAYER_VIEW_HOME)
        {
            bool rendered = videoPlatformReady && player_view_render_frame();
            if (!rendered && (!videoPlatformReady || !player_view_has_foreground_renderer()))
            {
                render_home_view(&home_state);
                consoleUpdate(NULL);
            }
        }
        else
        {
            if (have_snapshot)
            {
                if (!snapshot.has_media)
                {
                    player_ui_clear(&video_ui);
                }
                else
                    player_ui_sync(&video_ui, &snapshot);

                if (touch_tap)
                {
                    if (snapshot.has_media)
                    {
                        bool overlay_visible = player_ui_overlay_visible(&video_ui);
                        if (overlay_visible && main_touch_center_button_hit((int)touch_tap_x, (int)touch_tap_y))
                        {
                            main_input_trace("[input] action=touch-toggle-pause begin x=%d y=%d state=%s\n",
                                             (int)touch_tap_x,
                                             (int)touch_tap_y,
                                             main_player_state_name(snapshot.state));
                            bool ok = player_ui_toggle_pause(&video_ui, &snapshot);
                            main_input_trace("[input] action=touch-toggle-pause done ok=%d state=%s\n",
                                             ok ? 1 : 0,
                                             main_player_state_name(snapshot.state));
                        }
                        else if (overlay_visible)
                        {
                            main_input_trace("[input] action=touch-hide-controls x=%d y=%d state=%s\n",
                                             (int)touch_tap_x,
                                             (int)touch_tap_y,
                                             main_player_state_name(snapshot.state));
                            player_ui_hide_overlay(&video_ui);
                        }
                        else
                        {
                            main_input_trace("[input] action=touch-show-controls begin x=%d y=%d state=%s\n",
                                             (int)touch_tap_x,
                                             (int)touch_tap_y,
                                             main_player_state_name(snapshot.state));
                            int duration = player_ui_show_controls(&video_ui, &snapshot);
                            main_input_trace("[input] action=touch-show-controls done duration_ms=%d state=%s\n",
                                             duration,
                                             main_player_state_name(snapshot.state));
                        }
                    }
                    else
                    {
                        main_input_trace("[input] action=touch-show-controls skip reason=no-media\n");
                    }
                }

                padRepeaterUpdate(&video_repeater,
                                  kHeld & (HidNpadButton_L | HidNpadButton_R |
                                           HidNpadButton_Left | HidNpadButton_Right |
                                           HidNpadButton_Up | HidNpadButton_Down));
                u64 repeated_video = padRepeaterGetButtons(&video_repeater);
                u64 video_nav = kDown | repeated_video;

                if (kDown & HidNpadButton_A)
                {
                    main_input_trace("[input] action=toggle-pause begin state=%s\n", main_player_state_name(snapshot.state));
                    bool ok = player_ui_toggle_pause(&video_ui, &snapshot);
                    main_input_trace("[input] action=toggle-pause done ok=%d state=%s\n",
                                     ok ? 1 : 0,
                                     main_player_state_name(snapshot.state));
                }

                if (kDown & HidNpadButton_Minus)
                {
                    main_input_trace("[input] action=show-controls begin state=%s\n", main_player_state_name(snapshot.state));
                    int duration = player_ui_show_controls(&video_ui, &snapshot);
                    main_input_trace("[input] action=show-controls done duration_ms=%d state=%s\n",
                                     duration,
                                     main_player_state_name(snapshot.state));
                }

                if (video_nav & (HidNpadButton_L | HidNpadButton_Left))
                {
                    main_input_trace("[input] action=seek begin delta_ms=%d state=%s\n",
                                     -PLAYER_UI_SEEK_STEP_MS,
                                     main_player_state_name(snapshot.state));
                    bool ok = player_ui_seek(&video_ui, &snapshot, -PLAYER_UI_SEEK_STEP_MS);
                    main_input_trace("[input] action=seek done ok=%d delta_ms=%d state=%s\n",
                                     ok ? 1 : 0,
                                     -PLAYER_UI_SEEK_STEP_MS,
                                     main_player_state_name(snapshot.state));
                }
                if (video_nav & (HidNpadButton_R | HidNpadButton_Right))
                {
                    main_input_trace("[input] action=seek begin delta_ms=%d state=%s\n",
                                     PLAYER_UI_SEEK_STEP_MS,
                                     main_player_state_name(snapshot.state));
                    bool ok = player_ui_seek(&video_ui, &snapshot, PLAYER_UI_SEEK_STEP_MS);
                    main_input_trace("[input] action=seek done ok=%d delta_ms=%d state=%s\n",
                                     ok ? 1 : 0,
                                     PLAYER_UI_SEEK_STEP_MS,
                                     main_player_state_name(snapshot.state));
                }
                if (video_nav & HidNpadButton_Up)
                {
                    main_input_trace("[input] action=volume begin delta=%d state=%s volume=%d mute=%d\n",
                                     PLAYER_UI_VOLUME_STEP,
                                     main_player_state_name(snapshot.state),
                                     snapshot.volume,
                                     snapshot.mute ? 1 : 0);
                    bool ok = player_ui_change_volume(&video_ui, &snapshot, PLAYER_UI_VOLUME_STEP);
                    main_input_trace("[input] action=volume done ok=%d delta=%d state=%s\n",
                                     ok ? 1 : 0,
                                     PLAYER_UI_VOLUME_STEP,
                                     main_player_state_name(snapshot.state));
                }
                if (video_nav & HidNpadButton_Down)
                {
                    main_input_trace("[input] action=volume begin delta=%d state=%s volume=%d mute=%d\n",
                                     -PLAYER_UI_VOLUME_STEP,
                                     main_player_state_name(snapshot.state),
                                     snapshot.volume,
                                     snapshot.mute ? 1 : 0);
                    bool ok = player_ui_change_volume(&video_ui, &snapshot, -PLAYER_UI_VOLUME_STEP);
                    main_input_trace("[input] action=volume done ok=%d delta=%d state=%s\n",
                                     ok ? 1 : 0,
                                     -PLAYER_UI_VOLUME_STEP,
                                     main_player_state_name(snapshot.state));
                }

                if (video_stick_repeat_cooldown > 0)
                    --video_stick_repeat_cooldown;
                else
                {
                    HidAnalogStickState l = padGetStickPos(&pad, 0);
                    HidAnalogStickState r = padGetStickPos(&pad, 1);
                    int stick_x = abs(r.x) > abs(l.x) ? r.x : l.x;
                    int stick_y = abs(r.y) > abs(l.y) ? r.y : l.y;

                    if (stick_x <= -PLAYER_UI_STICK_THRESHOLD)
                    {
                        main_input_trace("[input] action=stick-seek begin delta_ms=%d stick_x=%d state=%s\n",
                                         -PLAYER_UI_SEEK_STEP_MS,
                                         stick_x,
                                         main_player_state_name(snapshot.state));
                        bool ok = player_ui_seek(&video_ui, &snapshot, -PLAYER_UI_SEEK_STEP_MS);
                        main_input_trace("[input] action=stick-seek done ok=%d delta_ms=%d\n",
                                         ok ? 1 : 0,
                                         -PLAYER_UI_SEEK_STEP_MS);
                        video_stick_repeat_cooldown = 5;
                    }
                    else if (stick_x >= PLAYER_UI_STICK_THRESHOLD)
                    {
                        main_input_trace("[input] action=stick-seek begin delta_ms=%d stick_x=%d state=%s\n",
                                         PLAYER_UI_SEEK_STEP_MS,
                                         stick_x,
                                         main_player_state_name(snapshot.state));
                        bool ok = player_ui_seek(&video_ui, &snapshot, PLAYER_UI_SEEK_STEP_MS);
                        main_input_trace("[input] action=stick-seek done ok=%d delta_ms=%d\n",
                                         ok ? 1 : 0,
                                         PLAYER_UI_SEEK_STEP_MS);
                        video_stick_repeat_cooldown = 5;
                    }
                    else if (stick_y >= PLAYER_UI_STICK_THRESHOLD)
                    {
                        main_input_trace("[input] action=stick-volume begin delta=%d stick_y=%d state=%s volume=%d mute=%d\n",
                                         PLAYER_UI_VOLUME_STEP,
                                         stick_y,
                                         main_player_state_name(snapshot.state),
                                         snapshot.volume,
                                         snapshot.mute ? 1 : 0);
                        bool ok = player_ui_change_volume(&video_ui, &snapshot, PLAYER_UI_VOLUME_STEP);
                        main_input_trace("[input] action=stick-volume done ok=%d delta=%d\n",
                                         ok ? 1 : 0,
                                         PLAYER_UI_VOLUME_STEP);
                        video_stick_repeat_cooldown = 5;
                    }
                    else if (stick_y <= -PLAYER_UI_STICK_THRESHOLD)
                    {
                        main_input_trace("[input] action=stick-volume begin delta=%d stick_y=%d state=%s volume=%d mute=%d\n",
                                         -PLAYER_UI_VOLUME_STEP,
                                         stick_y,
                                         main_player_state_name(snapshot.state),
                                         snapshot.volume,
                                         snapshot.mute ? 1 : 0);
                        bool ok = player_ui_change_volume(&video_ui, &snapshot, -PLAYER_UI_VOLUME_STEP);
                        main_input_trace("[input] action=stick-volume done ok=%d delta=%d\n",
                                         ok ? 1 : 0,
                                         -PLAYER_UI_VOLUME_STEP);
                        video_stick_repeat_cooldown = 5;
                    }
                }
            }

            bool trace_render_frame = touch_tap || kDown != 0;
            if (trace_render_frame)
            {
                main_input_trace("[render] frame begin reason=input view=%s state=%s media=%d\n",
                                 player_view_mode_name(active_view),
                                 have_snapshot ? main_player_state_name(snapshot.state) : "none",
                                 have_snapshot && snapshot.has_media ? 1 : 0);
            }
            bool rendered = player_view_render_frame();
            if (trace_render_frame)
            {
                main_input_trace("[render] frame done rendered=%d view=%s state=%s media=%d\n",
                                 rendered ? 1 : 0,
                                 player_view_mode_name(active_view),
                                 have_snapshot ? main_player_state_name(snapshot.state) : "none",
                                 have_snapshot && snapshot.has_media ? 1 : 0);
            }
        }

        if (have_snapshot)
            player_snapshot_clear(&snapshot);
    }

    if (exit_reason == EXIT_REASON_UNKNOWN)
        exit_reason = EXIT_REASON_APPLET_LOOP_ENDED;

    log_set_stdio_mirror(false);
    shutdown_stdio_trace("[INFO] [shutdown] begin reason=%s network_ready=%d dlna_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
                         exit_reason_name(exit_reason),
                         networkReady ? 1 : 0,
                         dlnaRunning ? 1 : 0,
                         videoRenderReady ? 1 : 0,
                         g_nxlinkSock,
                         storageReady ? 1 : 0);
    log_info("[shutdown] begin reason=%s network_ready=%d dlna_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
             exit_reason_name(exit_reason),
             networkReady ? 1 : 0,
             dlnaRunning ? 1 : 0,
             videoRenderReady ? 1 : 0,
             g_nxlinkSock,
             storageReady ? 1 : 0);

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

    if (rendererPrestarted && !dlnaRunning)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=player_deinit begin reason=prestarted-without-dlna\n");
        log_info("[shutdown] step=player_deinit begin reason=prestarted-without-dlna\n");
        player_deinit();
        shutdown_stdio_trace("[INFO] [shutdown] step=player_deinit done\n");
        log_info("[shutdown] step=player_deinit done\n");
    }

    shutdown_stdio_trace("[INFO] [shutdown] step=log_runtime_shutdown begin\n");
    log_info("[shutdown] step=log_runtime_shutdown begin\n");
    log_runtime_shutdown();
    shutdown_stdio_trace("[INFO] [shutdown] step=log_runtime_shutdown done\n");
    set_power_policy(false, false);

    if (g_nxlinkSock >= 0)
        shutdown_stdio_trace("[INFO] [shutdown] step=nxlink_close defer reason=userAppExit fd=%d\n", g_nxlinkSock);
    else
        shutdown_stdio_trace("[INFO] [shutdown] step=nxlink_close skip reason=no-fd\n");

    if (networkReady)
        shutdown_stdio_trace("[INFO] [shutdown] step=socketExit defer reason=userAppExit\n");

    if (g_consoleInitialized)
        shutdown_stdio_trace("[INFO] [shutdown] step=consoleExit defer reason=userAppExit\n");

    shutdown_stdio_trace("[INFO] [shutdown] step=main return begin\n");
    return 0;
}
