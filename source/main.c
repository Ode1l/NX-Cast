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

#include "app/protocol_coordinator.h"
#include "iptv/iptv.h"
#include "log/log.h"
#include "player/player.h"
#include "player/ui/channel_list.h"
#include "player/ui/layout.h"
#include "player/ui/ui.h"
#include "player/view.h"
#include "protocol/airplay/discovery/mdns.h"
#include "protocol/airplay/integration.h"
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
#define RETURN_HOME_STOP_GRACE_MS 250ULL
#define RUNTIME_HEARTBEAT_INTERVAL_MS 2000ULL

#ifndef NXCAST_DIAG_PROFILE_ID
#define NXCAST_DIAG_PROFILE_ID 0
#endif
#ifndef NXCAST_DIAG_PROFILE_NAME
#define NXCAST_DIAG_PROFILE_NAME "normal"
#endif

static int g_nxlinkSock = -1;
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

static void main_log_protocol_transition(
    const ProtocolCoordinatorSnapshot *previous,
    const ProtocolCoordinatorSnapshot *current)
{
    if (!previous || !current || previous->revision == current->revision)
        return;

    log_info("[protocol-coordinator] revision=%u->%u state=%s->%s "
             "iptv=%s->%s dlna=%s->%s airplay=%s->%s "
             "owner=%s->%s token=%llu generation=%u pin_visible=%d "
             "airplay_running=%d airplay_starting=%d status=%s\n",
             previous->revision,
             current->revision,
             protocol_coordinator_state_name(previous->state),
             protocol_coordinator_state_name(current->state),
             protocol_service_state_name(previous->services[PROTOCOL_SERVICE_IPTV]),
             protocol_service_state_name(current->services[PROTOCOL_SERVICE_IPTV]),
             protocol_service_state_name(previous->services[PROTOCOL_SERVICE_DLNA]),
             protocol_service_state_name(current->services[PROTOCOL_SERVICE_DLNA]),
             protocol_service_state_name(previous->services[PROTOCOL_SERVICE_AIRPLAY]),
             protocol_service_state_name(current->services[PROTOCOL_SERVICE_AIRPLAY]),
             player_media_owner_name(previous->active_media.owner),
             player_media_owner_name(current->active_media.owner),
             (unsigned long long)current->active_media.token,
             current->active_media.generation,
             current->airplay.pin_visible ? 1 : 0,
             current->airplay.running ? 1 : 0,
             current->airplay.starting ? 1 : 0,
             current->airplay.status[0] ? current->airplay.status : "none");
}

static bool main_request_player_home(const PlayerSnapshot *snapshot,
                                     uint64_t now_ms,
                                     bool *pending,
                                     uint64_t *ready_ms,
                                     bool *stop_dispatched)
{
    bool playback_active;

    if (!pending || !ready_ms || !stop_dispatched)
        return false;
    *stop_dispatched = false;
    playback_active = snapshot && snapshot->has_media &&
                      snapshot->state != PLAYER_STATE_IDLE &&
                      snapshot->state != PLAYER_STATE_STOPPED;
    log_info("[return-home] phase=request t_ms=%llu playback_active=%d "
             "state=%s media=%d\n",
             (unsigned long long)now_ms, playback_active ? 1 : 0,
             snapshot ? main_player_state_name(snapshot->state) : "none",
             snapshot && snapshot->has_media ? 1 : 0);
    if (!playback_active)
    {
        bool shown;

        *pending = false;
        log_info("[return-home] phase=show-home begin reason=no-active-media\n");
        shown = player_view_show_home();
        log_info("[return-home] phase=show-home done ok=%d reason=no-active-media\n",
                 shown ? 1 : 0);
        return shown;
    }
    log_info("[return-home] phase=player-stop begin state=%s\n",
             main_player_state_name(snapshot->state));
    {
        PlayerCommandRequest stop_request = {
            .kind = PLAYER_COMMAND_STOP_ANY,
            .source = PLAYER_COMMAND_SOURCE_UI,
        };
        PlayerCommandStatus stop_status =
            player_submit_command_async(&stop_request);

        if (!player_command_status_succeeded(stop_status))
        {
            log_info("[return-home] phase=player-stop failed state=%s status=%s\n",
                     main_player_state_name(snapshot->state),
                     player_command_status_name(stop_status));
            return false;
        }
    }
    log_info("[return-home] phase=player-stop done state=%s\n",
             main_player_state_name(snapshot->state));
    *stop_dispatched = true;
    *pending = true;
    *ready_ms = now_ms + RETURN_HOME_STOP_GRACE_MS;
    log_info("[return-home] phase=wait-player-stopped ready_ms=%llu\n",
             (unsigned long long)*ready_ms);
    return true;
}

static LogLevel main_input_trace_level(void)
{
    return LOG_LEVEL_INFO;
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

static bool main_touch_video_action_hints_hit(int x, int y)
{
    int width;
    int height;
    int center_y;

    if (!main_get_video_layout(NULL, &width, &height))
        return false;

    center_y = height - 52;
    return x >= width * 3 / 5 &&
           x < width &&
           y >= center_y - 28 &&
           y < height;
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

static bool main_snapshot_playback_active(const PlayerSnapshot *snapshot)
{
    if (!snapshot || !snapshot->has_media)
        return false;

    return snapshot->state == PLAYER_STATE_LOADING ||
           snapshot->state == PLAYER_STATE_BUFFERING ||
           snapshot->state == PLAYER_STATE_SEEKING ||
           snapshot->state == PLAYER_STATE_PLAYING ||
           snapshot->state == PLAYER_STATE_PAUSED;
}

static void build_home_view_state(PlayerHomeViewState *out,
                                  bool storage_ready,
                                  bool network_ready,
                                  const ProtocolCoordinatorSnapshot *protocols,
                                  bool video_ready,
                                  bool iptv_panel_open,
                                  bool iptv_sources_open,
                                  const PlayerSnapshot *snapshot)
{
    IptvState iptv_state = {0};

    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->storage_ready = storage_ready;
    out->network_ready = network_ready;
    out->dlna_running = protocols &&
                        protocols->services[PROTOCOL_SERVICE_DLNA] ==
                            PROTOCOL_SERVICE_RUNNING;
    out->airplay_running = protocols && protocols->airplay.running;
    out->airplay_pin_visible = protocols && protocols->airplay.pin_visible;
    if (protocols)
    {
        snprintf(out->airplay_pin, sizeof(out->airplay_pin), "%s",
                 protocols->airplay.pin);
        snprintf(out->airplay_status, sizeof(out->airplay_status), "%s",
                 protocols->airplay.status);
    }
    out->video_ready = video_ready;
    out->playback_active = main_snapshot_playback_active(snapshot);
    out->playback_state = snapshot ? snapshot->state : PLAYER_STATE_IDLE;
    out->has_error = get_latest_error_line(out->error_line, sizeof(out->error_line));
    out->iptv_panel_open = iptv_panel_open;
    out->iptv_sources_open = iptv_sources_open;
    if (iptv_get_state(&iptv_state))
    {
        out->iptv_ready = protocols &&
                          protocols->services[PROTOCOL_SERVICE_IPTV] ==
                              PROTOCOL_SERVICE_RUNNING &&
                          iptv_state.initialized && iptv_state.loaded;
        out->iptv_refreshing = iptv_state.refreshing;
        out->iptv_source_count = iptv_state.source_count;
        out->iptv_channel_count = iptv_state.channel_count;
        out->iptv_visible_count = iptv_state.visible_count;
        out->iptv_favorite_count = iptv_state.favorite_count;
        out->iptv_recent_count = iptv_state.recent_count;
        out->iptv_logo_cached_count = iptv_state.logo_cached_count;
        out->iptv_epg_channel_count = iptv_state.epg_channel_count;
        out->iptv_selected_index = iptv_state.selected_index;
        out->iptv_source_selected_index = iptv_state.source_selected_index;
        snprintf(out->iptv_active_filter, sizeof(out->iptv_active_filter), "%s", iptv_state.active_filter);
        snprintf(out->iptv_search, sizeof(out->iptv_search), "%s", iptv_state.search);
        snprintf(out->iptv_status, sizeof(out->iptv_status), "%s", iptv_state.status);
        snprintf(out->iptv_last_name, sizeof(out->iptv_last_name), "%s", iptv_state.last_name);
    }
}

static bool main_protocol_iptv_start(void *context)
{
    (void)context;
    return iptv_init();
}

static void main_protocol_iptv_stop(void *context)
{
    (void)context;
    iptv_deinit();
}

static bool main_protocol_dlna_start(void *context)
{
    (void)context;
    return dlna_control_start();
}

static void main_protocol_dlna_stop(void *context)
{
    (void)context;
    dlna_control_stop();
}

static bool main_protocol_airplay_start(void *context)
{
    (void)context;
    return airplay_integration_start();
}

static void main_protocol_airplay_request_stop(void *context)
{
    (void)context;
    airplay_integration_request_stop();
}

static void main_protocol_airplay_stop(void *context)
{
    (void)context;
    airplay_integration_stop();
}

static bool main_protocol_airplay_get_status(void *context,
                                             ProtocolAirPlayStatus *status_out)
{
    AirPlayIntegrationStatus status = {0};

    (void)context;
    if (!status_out || !airplay_integration_get_status(&status))
        return false;

    memset(status_out, 0, sizeof(*status_out));
    status_out->running = status.running;
    status_out->starting = status.starting;
    status_out->pin_visible = status.pin_visible;
    memcpy(status_out->pin, status.pin, sizeof(status_out->pin));
    snprintf(status_out->status, sizeof(status_out->status), "%s", status.status);
    return true;
}

typedef enum
{
    MAIN_IPTV_URL_NONE = 0,
    MAIN_IPTV_URL_PLAYING,
    MAIN_IPTV_URL_SOURCE_QUEUED
} MainIptvUrlResult;

static MainIptvUrlResult main_iptv_prompt_and_open(bool player_ready)
{
    char url[IPTV_URL_MAX];

    if (!iptv_prompt_url(url, sizeof(url)))
        return MAIN_IPTV_URL_NONE;
    if (iptv_url_looks_like_playlist(url))
        return iptv_add_source_url(url) ? MAIN_IPTV_URL_SOURCE_QUEUED : MAIN_IPTV_URL_NONE;
    if (!player_ready)
    {
        iptv_set_status("Player is not ready. Check the media toolchain and restart NX-Cast.");
        return MAIN_IPTV_URL_NONE;
    }
    return iptv_play_url(url) ? MAIN_IPTV_URL_PLAYING : MAIN_IPTV_URL_NONE;
}

static u64 main_normalize_controller_buttons(u64 buttons)
{
    u64 normalized = buttons;

    // A single horizontal Joy-Con has no complete A/B pair. SR confirms and
    // SL returns, while its stick remains available for all list navigation.
    if (buttons & HidNpadButton_AnySR)
        normalized |= HidNpadButton_A;
    if (buttons & HidNpadButton_AnySL)
        normalized |= HidNpadButton_B;
    return normalized;
}

static u64 main_iptv_navigation_buttons(u64 held_buttons, const PadState *pad)
{
    u64 navigation = held_buttons & (HidNpadButton_L | HidNpadButton_R);

    if (held_buttons & HidNpadButton_Up)
        navigation |= HidNpadButton_Up;
    if (held_buttons & HidNpadButton_Down)
        navigation |= HidNpadButton_Down;
    if (held_buttons & HidNpadButton_Left)
        navigation |= HidNpadButton_L;
    if (held_buttons & HidNpadButton_Right)
        navigation |= HidNpadButton_R;

    if (pad && navigation == 0)
    {
        HidAnalogStickState left_stick = padGetStickPos(pad, 0);
        HidAnalogStickState right_stick = padGetStickPos(pad, 1);
        int stick_x = abs(right_stick.x) > abs(left_stick.x) ? right_stick.x : left_stick.x;
        int stick_y = abs(right_stick.y) > abs(left_stick.y) ? right_stick.y : left_stick.y;

        if (abs(stick_y) >= abs(stick_x))
        {
            if (stick_y >= PLAYER_UI_STICK_THRESHOLD)
                navigation |= HidNpadButton_Up;
            else if (stick_y <= -PLAYER_UI_STICK_THRESHOLD)
                navigation |= HidNpadButton_Down;
        }
        else
        {
            if (stick_x <= -PLAYER_UI_STICK_THRESHOLD)
                navigation |= HidNpadButton_L;
            else if (stick_x >= PLAYER_UI_STICK_THRESHOLD)
                navigation |= HidNpadButton_R;
        }
    }

    return navigation;
}

static void main_iptv_select_page(bool sources_open, int page_delta)
{
    const int delta = page_delta * PLAYER_IPTV_VISIBLE_ROWS;
    if (sources_open)
        iptv_select_source_delta(delta);
    else
        iptv_select_delta(delta);
}

static bool main_iptv_play_selected_channel(bool player_ready, bool *panel_open)
{
    bool ok = false;
    int selected_index = iptv_get_selected_index();

    if (!player_ready)
        iptv_set_status("Player is not ready. Check the media toolchain and restart NX-Cast.");
    else
        ok = iptv_play_channel(selected_index);
    main_input_trace("[input] action=iptv-play ok=%d index=%d\n", ok ? 1 : 0, selected_index);
    if (ok)
    {
        if (panel_open)
            *panel_open = false;
        (void)player_view_show_video();
    }
    return ok;
}

static bool main_iptv_handle_panel_touch_tap(int x,
                                              int y,
                                              bool player_ready,
                                              bool *panel_open,
                                              bool *sources_open)
{
    int selected_index;
    int item_count;
    int touched_index;

    if (!panel_open || !sources_open || !*panel_open)
        return false;

    if (player_iptv_close_hit(x, y, *sources_open))
    {
        *panel_open = false;
        main_input_trace("[input] action=touch-iptv-close x=%d y=%d\n", x, y);
        return true;
    }
    if (player_iptv_point_in_rect(x,
                                  y,
                                  PLAYER_IPTV_CHANNEL_TAB_LEFT,
                                  PLAYER_IPTV_TAB_TOP,
                                  PLAYER_IPTV_CHANNEL_TAB_RIGHT,
                                  PLAYER_IPTV_TAB_BOTTOM))
    {
        *sources_open = false;
        main_input_trace("[input] action=touch-iptv-tab page=channels x=%d y=%d\n", x, y);
        return true;
    }
    if (player_iptv_point_in_rect(x,
                                  y,
                                  PLAYER_IPTV_SOURCE_TAB_LEFT,
                                  PLAYER_IPTV_TAB_TOP,
                                  PLAYER_IPTV_SOURCE_TAB_RIGHT,
                                  PLAYER_IPTV_TAB_BOTTOM))
    {
        *sources_open = true;
        main_input_trace("[input] action=touch-iptv-tab page=sources x=%d y=%d\n", x, y);
        return true;
    }
    if (player_iptv_point_in_rect(x,
                                  y,
                                  PLAYER_IPTV_PAGE_PREV_LEFT,
                                  PLAYER_IPTV_PAGE_BUTTON_TOP,
                                  PLAYER_IPTV_PAGE_PREV_RIGHT,
                                  PLAYER_IPTV_PAGE_BUTTON_BOTTOM))
    {
        main_iptv_select_page(*sources_open, -1);
        main_input_trace("[input] action=touch-iptv-page delta=-1 page=%s\n",
                         *sources_open ? "sources" : "channels");
        return true;
    }
    if (player_iptv_point_in_rect(x,
                                  y,
                                  PLAYER_IPTV_PAGE_NEXT_LEFT,
                                  PLAYER_IPTV_PAGE_BUTTON_TOP,
                                  PLAYER_IPTV_PAGE_NEXT_RIGHT,
                                  PLAYER_IPTV_PAGE_BUTTON_BOTTOM))
    {
        main_iptv_select_page(*sources_open, 1);
        main_input_trace("[input] action=touch-iptv-page delta=1 page=%s\n",
                         *sources_open ? "sources" : "channels");
        return true;
    }

    selected_index = *sources_open ? iptv_get_source_selected_index() : iptv_get_selected_index();
    item_count = *sources_open ? iptv_get_source_count() : iptv_get_channel_count();
    touched_index = player_iptv_touch_row_index(x, y, selected_index, item_count);
    if (touched_index >= 0)
    {
        if (*sources_open)
        {
            if (touched_index == selected_index)
                (void)iptv_refresh_selected_source_async();
            else
                iptv_set_source_selected_index(touched_index);
        }
        else if (touched_index == selected_index)
        {
            (void)main_iptv_play_selected_channel(player_ready, panel_open);
        }
        else
        {
            iptv_set_selected_index(touched_index);
        }
        main_input_trace("[input] action=touch-iptv-row page=%s index=%d activate=%d\n",
                         *sources_open ? "sources" : "channels",
                         touched_index,
                         touched_index == selected_index ? 1 : 0);
        return true;
    }

    if (player_iptv_action_hit(x, y, *sources_open))
    {
        if (*sources_open)
            (void)iptv_refresh_selected_source_async();
        else
            (void)main_iptv_play_selected_channel(player_ready, panel_open);
        main_input_trace("[input] action=touch-iptv-primary page=%s\n",
                         *sources_open ? "sources" : "channels");
        return true;
    }
    return false;
}

static bool main_iptv_handle_panel_touch_swipe(int start_x,
                                                int start_y,
                                                int end_x,
                                                int end_y,
                                                bool sources_open)
{
    int page_delta = player_iptv_swipe_page_delta(start_x, start_y, end_x, end_y);
    if (page_delta == 0)
        return false;
    main_iptv_select_page(sources_open, page_delta);
    main_input_trace("[input] action=touch-iptv-swipe delta=%d page=%s start=%d,%d end=%d,%d\n",
                     page_delta,
                     sources_open ? "sources" : "channels",
                     start_x,
                     start_y,
                     end_x,
                     end_y);
    return true;
}

static void main_iptv_handle_panel_input(u64 k_down,
                                         u64 k_held,
                                         const PadState *pad,
                                         PadRepeater *repeater,
                                         bool player_ready,
                                         bool *panel_open,
                                         bool *sources_open)
{
    if (!repeater || !panel_open || !sources_open || !*panel_open)
        return;

    padRepeaterUpdate(repeater, main_iptv_navigation_buttons(k_held, pad));
    u64 iptv_nav = k_down | padRepeaterGetButtons(repeater);

    if (*sources_open)
    {
        if (iptv_nav & HidNpadButton_Up)
            iptv_select_source_delta(-1);
        if (iptv_nav & HidNpadButton_Down)
            iptv_select_source_delta(1);
        if (iptv_nav & HidNpadButton_L)
            main_iptv_select_page(true, -1);
        if (iptv_nav & HidNpadButton_R)
            main_iptv_select_page(true, 1);
        if (k_down & HidNpadButton_A)
            iptv_refresh_selected_source_async();
        if (k_down & HidNpadButton_Y)
            iptv_prompt_add_source();
        if (k_down & HidNpadButton_ZR)
            iptv_prompt_set_source_epg();
        if (k_down & HidNpadButton_Minus)
            iptv_remove_selected_source();
    }
    else
    {
        if (iptv_nav & HidNpadButton_Up)
            iptv_select_delta(-1);
        if (iptv_nav & HidNpadButton_Down)
            iptv_select_delta(1);
        if (iptv_nav & HidNpadButton_L)
            main_iptv_select_page(false, -1);
        if (iptv_nav & HidNpadButton_R)
            main_iptv_select_page(false, 1);
        if (k_down & HidNpadButton_ZL)
            iptv_cycle_filter(-1);
        if (k_down & HidNpadButton_ZR)
            iptv_cycle_filter(1);
        if (k_down & HidNpadButton_StickL)
            iptv_prompt_search();
        if (k_down & HidNpadButton_StickR)
            iptv_clear_search();
        if (k_down & HidNpadButton_Y)
            iptv_toggle_selected_favorite();
        if (k_down & HidNpadButton_Minus)
        {
            MainIptvUrlResult result = main_iptv_prompt_and_open(player_ready);
            if (result == MAIN_IPTV_URL_PLAYING)
            {
                *panel_open = false;
                (void)player_view_show_video();
            }
            else if (result == MAIN_IPTV_URL_SOURCE_QUEUED)
            {
                *panel_open = true;
                *sources_open = false;
            }
        }
        if (k_down & HidNpadButton_A)
            (void)main_iptv_play_selected_channel(player_ready, panel_open);
    }

    if (k_down & HidNpadButton_B)
    {
        *panel_open = false;
        main_input_trace("[input] action=iptv-panel open=0 reason=back\n");
    }
}

static void render_home_view(const PlayerHomeViewState *state)
{
    if (!state)
        return;

    consoleClear();
    printf("\n");
    printf(ANSI_ACCENT "  NX-CAST / HOME" ANSI_RESET "                           " ANSI_TEXT "DLNA + AIRPLAY" ANSI_RESET "\n");
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
    printf("  2. Tap Cast, DLNA, or AirPlay, then choose " ANSI_ACCENT "NX-Cast" ANSI_RESET ".\n");
    printf("  3. Wait for the loading spinner; playback controls appear on touch.\n\n");

    printf(ANSI_ACCENT "  [ PLAYER CONTROLS ]" ANSI_RESET "\n");
    printf("  A Play/Pause     B Home     L/R Seek 10s     Up/Down Volume     + Exit\n");
    printf("  Touch: show/hide UI. Drag timeline: preview, release to seek.\n\n");

    printf(ANSI_ACCENT "  [ IPTV ]" ANSI_RESET "\n");
    printf("  X Browse channels     - Open media/M3U URL     Y Reload playlists\n");
    if (state->playback_active)
        printf("  A Return to the active player\n");
    printf("  Channels:%d  Sources:%d  %s\n\n",
           state->iptv_channel_count,
           state->iptv_source_count,
           state->iptv_status);

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
    printf("AirPlay:%s%s%s  ",
           state->airplay_running ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->airplay_running),
           ANSI_RESET);
    printf("Player:%s%s%s\n\n",
           state->video_ready ? ANSI_ACCENT : ANSI_ERROR,
           ready_label(state->video_ready),
           ANSI_RESET);

    if (state->airplay_pin_visible)
    {
        printf(ANSI_ACCENT "  [ AIRPLAY PAIRING ]" ANSI_RESET "\n");
        printf("  Enter PIN " ANSI_TEXT "%s" ANSI_RESET " on your iPhone.\n\n",
               state->airplay_pin);
    }

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

static void enable_nxlink_logging(bool network_ready)
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
        // Keep the logger on stderr's duplicated descriptor. This is the
        // libnx path used by the known-good release and survives closure or
        // reuse of the original low-numbered nxlink descriptor.
        log_set_socket_mirror(STDERR_FILENO);
        log_set_stdio_mirror(true);
        log_info("[log] nxlink logger connected host=%s port=%d fd=%d mirror_fd=%d stdout=local\n",
                 inet_ntoa(__nxlink_host),
                 NXLINK_CLIENT_PORT,
                 g_nxlinkSock,
                 STDERR_FILENO);
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
        printf("[ERROR] [log] log_runtime_init failed\n");
    set_power_policy(true, true);

    bool storageReady = dlna_resource_store_ensure_defaults();
    if (storageReady)
        log_info("[storage] DLNA resources ready on SD.\n");
    else
        log_warn("[storage] failed to prepare DLNA resources on SD.\n");

    bool networkReady = initialize_network();
    g_networkInitialized = networkReady;
    enable_nxlink_logging(networkReady);
    log_info("[diagnostic] profile_id=%d profile=%s protocol_start=%s\n",
             NXCAST_DIAG_PROFILE_ID, NXCAST_DIAG_PROFILE_NAME,
#if defined(NXCAST_PROTOCOL_START_SERIAL) && NXCAST_PROTOCOL_START_SERIAL
             "serial"
#else
             "parallel"
#endif
    );
    ProtocolCoordinatorSnapshot protocolStatus = {0};
    ProtocolCoordinatorSnapshot loggedProtocolStatus = {0};
    bool videoPlatformReady = player_view_init();
    bool rendererPrestarted = false;
    bool videoRenderReady = false;
    bool playerActivationAttempted = false;
    bool playerCommandsActive = false;
    bool protocolCoordinatorStartAttempted = false;
    bool protocolCoordinatorStarted = false;
    bool airplayRuntimeAllowed = true;

    if (videoPlatformReady)
    {
        rendererPrestarted = player_init();
        if (!rendererPrestarted)
        {
            playerActivationAttempted = true;
            log_warn("[ui] player actor start failed; media actions may fail.\n");
        }
    }
    else
    {
        playerActivationAttempted = true;
    }

    ProtocolCoordinatorConfig protocolConfig = {
        .enabled = {
            [PROTOCOL_SERVICE_IPTV] = true,
            [PROTOCOL_SERVICE_DLNA] = networkReady,
            [PROTOCOL_SERVICE_AIRPLAY] = false
        },
#if defined(NXCAST_PROTOCOL_START_SERIAL) && NXCAST_PROTOCOL_START_SERIAL
        .serial_startup = true,
#endif
    };
    ProtocolCoordinatorOperations protocolOperations = {
        .context = NULL,
        .services = {
            [PROTOCOL_SERVICE_IPTV] = {
                .start = main_protocol_iptv_start,
                .stop = main_protocol_iptv_stop,
                .stop_after_start_attempt = true
            },
            [PROTOCOL_SERVICE_DLNA] = {
                .start = main_protocol_dlna_start,
                .stop = main_protocol_dlna_stop
            },
            [PROTOCOL_SERVICE_AIRPLAY] = {
                .start = main_protocol_airplay_start,
                .request_stop = main_protocol_airplay_request_stop,
                .stop = main_protocol_airplay_stop
            }
        },
        .airplay_get_status = main_protocol_airplay_get_status
    };
#if defined(NXCAST_DISABLE_AIRPLAY_RUNTIME) && NXCAST_DISABLE_AIRPLAY_RUNTIME
    airplayRuntimeAllowed = false;
    log_warn("[airplay] runtime disabled by playback baseline build\n");
#endif
    protocol_coordinator_reset();
    (void)protocol_coordinator_get_snapshot(&protocolStatus);
    loggedProtocolStatus = protocolStatus;

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    hidInitializeTouchScreen();

    PadState pad;
    padInitializeAny(&pad);
    PadRepeater video_repeater;
    padRepeaterInitialize(&video_repeater, 20, 15);
    PadRepeater iptv_repeater;
    padRepeaterInitialize(&iptv_repeater, 20, 6);

    int video_stick_repeat_cooldown = 0;
    TouchTraceState touch_trace = {0};
    TouchSeekState touch_seek = {0};
    uint64_t last_touch_tap_ms = 0;
    PlayerViewMode last_logged_view = PLAYER_VIEW_HOME;
    bool have_logged_view = false;
    bool iptv_panel_open = false;
    bool iptv_sources_open = false;
    bool return_home_pending = false;
    bool airplay_pin_was_visible = false;
    uint64_t return_home_ready_ms = 0;
#if defined(NXCAST_INPUT_TRACE_VERBOSE) && NXCAST_INPUT_TRACE_VERBOSE
    uint64_t runtime_heartbeat_due_ms = main_monotonic_time_ms();
    uint64_t runtime_frame = 0;
#endif
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
        uint64_t input_now_ms = main_monotonic_time_ms();

#if defined(NXCAST_INPUT_TRACE_VERBOSE) && NXCAST_INPUT_TRACE_VERBOSE
        runtime_frame++;
#endif

        padUpdate(&pad);
        if (rendererPrestarted && !playerActivationAttempted)
        {
            PlayerRuntimeHealth player_health = {0};

            if (player_get_runtime_health(&player_health))
            {
                if (player_health.backend_ready)
                {
                    videoRenderReady = player_view_prepare_video();
                    playerCommandsActive = player_activate();
                    playerActivationAttempted = true;
                    log_info("[ui] video render prepare=%d player_activate=%d before_media_url=1\n",
                             videoRenderReady ? 1 : 0,
                             playerCommandsActive ? 1 : 0);
                }
                else if (player_health.initialization_failed ||
                         (!player_health.running &&
                          !player_health.initializing))
                {
                    playerActivationAttempted = true;
                    log_error("[ui] player backend initialization failed; media actions unavailable.\n");
                }
            }
        }
        if (!protocolCoordinatorStartAttempted && playerActivationAttempted)
        {
            protocolConfig.enabled[PROTOCOL_SERVICE_AIRPLAY] =
                airplayRuntimeAllowed && networkReady && playerCommandsActive &&
                videoRenderReady;
            protocolCoordinatorStarted =
                protocol_coordinator_start(&protocolConfig,
                                           &protocolOperations);
            protocolCoordinatorStartAttempted = true;
            (void)protocol_coordinator_get_snapshot(&protocolStatus);
            loggedProtocolStatus = protocolStatus;
            log_info("[protocol-coordinator] start accepted=%d state=%s iptv=%s dlna=%s airplay=%s network=%d root=%s\n",
                     protocolCoordinatorStarted ? 1 : 0,
                     protocol_coordinator_state_name(protocolStatus.state),
                     protocol_service_state_name(protocolStatus.services[PROTOCOL_SERVICE_IPTV]),
                     protocol_service_state_name(protocolStatus.services[PROTOCOL_SERVICE_DLNA]),
                     protocol_service_state_name(protocolStatus.services[PROTOCOL_SERVICE_AIRPLAY]),
                     networkReady ? 1 : 0,
                     IPTV_ROOT_DIR);
        }
        protocol_coordinator_tick();
        (void)protocol_coordinator_get_snapshot(&protocolStatus);
        main_log_protocol_transition(&loggedProtocolStatus, &protocolStatus);
        if (loggedProtocolStatus.revision != protocolStatus.revision)
            loggedProtocolStatus = protocolStatus;
        if (videoPlatformReady && player_get_snapshot(&snapshot))
            have_snapshot = true;
        if (videoPlatformReady && protocolStatus.airplay.pin_visible &&
            !airplay_pin_was_visible)
        {
            bool stop_dispatched = false;
            bool ok = main_request_player_home(have_snapshot ? &snapshot : NULL,
                                               input_now_ms,
                                               &return_home_pending,
                                               &return_home_ready_ms,
                                               &stop_dispatched);
            main_input_trace("[input] action=airplay-pin-home ok=%d stop=%d state=%s\n",
                             ok ? 1 : 0,
                             stop_dispatched ? 1 : 0,
                             have_snapshot ? main_player_state_name(snapshot.state) : "none");
            airplay_pin_was_visible = ok;
        }
        else
        {
            airplay_pin_was_visible = protocolStatus.airplay.pin_visible;
        }
        if (videoPlatformReady && return_home_pending &&
            input_now_ms >= return_home_ready_ms &&
            (!have_snapshot || !snapshot.has_media ||
             snapshot.state == PLAYER_STATE_IDLE ||
             snapshot.state == PLAYER_STATE_STOPPED))
        {
            log_info("[return-home] phase=show-home begin reason=player-stopped "
                     "state=%s media=%d\n",
                     have_snapshot ? main_player_state_name(snapshot.state) : "none",
                     have_snapshot && snapshot.has_media ? 1 : 0);
            bool ok = player_view_show_home();
            if (ok)
                return_home_pending = false;
            log_info("[return-home] phase=show-home done ok=%d "
                     "reason=player-stopped pending=%d\n",
                     ok ? 1 : 0, return_home_pending ? 1 : 0);
            main_input_trace("[input] action=return-home-complete ok=%d state=%s\n",
                             ok ? 1 : 0,
                             have_snapshot ? main_player_state_name(snapshot.state) : "none");
        }
        build_home_view_state(&home_state,
                              storageReady,
                              networkReady,
                              &protocolStatus,
                              videoRenderReady,
                              iptv_panel_open,
                              iptv_sources_open,
                              have_snapshot ? &snapshot : NULL);

        PlayerViewMode active_view = PLAYER_VIEW_HOME;
        if (videoRenderReady)
        {
            player_view_set_home_state(&home_state);
            if (have_snapshot)
                player_view_sync(&snapshot);
            player_view_begin_frame();
            active_view = player_view_get_mode();
        }

#if defined(NXCAST_INPUT_TRACE_VERBOSE) && NXCAST_INPUT_TRACE_VERBOSE
        if (input_now_ms >= runtime_heartbeat_due_ms)
        {
            LogRuntimeStats log_stats = {0};
            PlayerRuntimeHealth player_health = {0};
            AirPlayMdnsDiagnostics mdns_diagnostics = {0};

            (void)log_get_runtime_stats(&log_stats);
            (void)player_get_runtime_health(&player_health);
            (void)airplay_mdns_get_diagnostics(&mdns_diagnostics);
            log_info("[runtime-heartbeat] t_ms=%llu frame=%llu view=%s "
                     "player_state=%s media=%d return_home_pending=%d "
                     "protocol_state=%s owner=%s generation=%u "
                     "service_workers=%d,%d,%d service_age_ms=%llu,%llu,%llu "
                     "media_queue=%zu media_high=%zu media_command=%llu "
                     "media_kind=%d media_producer=%d media_token=%llu "
                     "media_generation=%u "
                     "media_command_age_ms=%llu media_heartbeat_age_ms=%llu "
                     "media_full=%llu media_stale=%llu media_timeout=%llu "
                     "media_coalesced=%llu media_accepting=%d "
                     "media_lifecycle=%d,%d,%d,%d "
                     "log_queue=%zu enqueued=%llu processed=%llu "
                     "log_high=%zu log_heartbeat_age_ms=%llu log_waiting=%d "
                     "queue_dropped=%llu mirror_dropped=%llu "
                     "mirror_failures=%llu mirror=%d worker=%d "
                     "mdns_phase=%s mdns_mode=%d mdns_running=%d "
                     "mdns_socket=%d mdns_heartbeat_age_ms=%llu "
                     "mdns_select=%llu mdns_recv=%llu mdns_sent=%llu\n",
                     (unsigned long long)input_now_ms,
                     (unsigned long long)runtime_frame,
                     player_view_mode_name(active_view),
                     have_snapshot ? main_player_state_name(snapshot.state) : "none",
                     have_snapshot && snapshot.has_media ? 1 : 0,
                     return_home_pending ? 1 : 0,
                     protocol_coordinator_state_name(protocolStatus.state),
                     player_media_owner_name(protocolStatus.active_media.owner),
                     protocolStatus.active_media.generation,
                     protocolStatus.service_worker_active[PROTOCOL_SERVICE_IPTV] ? 1 : 0,
                     protocolStatus.service_worker_active[PROTOCOL_SERVICE_DLNA] ? 1 : 0,
                     protocolStatus.service_worker_active[PROTOCOL_SERVICE_AIRPLAY] ? 1 : 0,
                     (unsigned long long)protocolStatus.service_transition_age_ms[PROTOCOL_SERVICE_IPTV],
                     (unsigned long long)protocolStatus.service_transition_age_ms[PROTOCOL_SERVICE_DLNA],
                     (unsigned long long)protocolStatus.service_transition_age_ms[PROTOCOL_SERVICE_AIRPLAY],
                     player_health.queue_depth,
                     player_health.queue_high_watermark,
                     (unsigned long long)player_health.current_command_id,
                     player_health.current_command_kind,
                     player_health.current_command_producer,
                     (unsigned long long)player_health.current_session_token,
                     player_health.current_generation,
                     (unsigned long long)player_health.current_command_age_ms,
                     (unsigned long long)player_health.heartbeat_age_ms,
                     (unsigned long long)player_health.rejected_full,
                     (unsigned long long)player_health.rejected_stale,
                     (unsigned long long)player_health.timed_out,
                     (unsigned long long)player_health.coalesced,
                     player_health.accepting ? 1 : 0,
                     player_health.initializing ? 1 : 0,
                     player_health.backend_ready ? 1 : 0,
                     player_health.initialization_failed ? 1 : 0,
                     player_health.dispatch_enabled ? 1 : 0,
                     log_stats.queue_depth,
                     (unsigned long long)log_stats.enqueued,
                     (unsigned long long)log_stats.processed,
                     log_stats.queue_high_watermark,
                     (unsigned long long)log_stats.worker_heartbeat_age_ms,
                     log_stats.worker_waiting ? 1 : 0,
                     (unsigned long long)log_stats.queue_dropped,
                     (unsigned long long)log_stats.mirror_dropped,
                     (unsigned long long)log_stats.mirror_failures,
                     log_stats.socket_mirror_enabled ? 1 : 0,
                     log_stats.worker_running ? 1 : 0,
                     airplay_mdns_phase_name(mdns_diagnostics.phase),
                     mdns_diagnostics.mode,
                     mdns_diagnostics.running ? 1 : 0,
                     mdns_diagnostics.socket_open ? 1 : 0,
                     (unsigned long long)mdns_diagnostics.worker_heartbeat_age_ms,
                     (unsigned long long)mdns_diagnostics.select_iterations,
                     (unsigned long long)mdns_diagnostics.packets_received,
                     (unsigned long long)mdns_diagnostics.packets_sent);
            runtime_heartbeat_due_ms = input_now_ms +
                                       RUNTIME_HEARTBEAT_INTERVAL_MS;
        }
#endif

        u64 kDown = main_normalize_controller_buttons(padGetButtonsDown(&pad));
        u64 kHeld = main_normalize_controller_buttons(padGetButtons(&pad));
        HidTouchScreenState touch_state = {0};
        size_t touch_total = hidGetTouchScreenStates(&touch_state, 1);
        bool touch_present = touch_total > 0 && touch_state.count > 0;
        bool touch_tap = false;
        s32 touch_x = 0;
        s32 touch_y = 0;
        s32 touch_tap_x = 0;
        s32 touch_tap_y = 0;
        bool touch_swipe = false;
        s32 touch_swipe_start_x = 0;
        s32 touch_swipe_start_y = 0;
        s32 touch_swipe_end_x = 0;
        s32 touch_swipe_end_y = 0;
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
                    !iptv_panel_open &&
                    have_snapshot &&
                    player_ui_overlay_visible(&video_ui) &&
                    !main_touch_video_action_hints_hit((int)touch_x, (int)touch_y) &&
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
            bool tap_view = touch_trace.start_view == active_view;
            bool tap_debounced = !iptv_panel_open &&
                                 last_touch_tap_ms != 0 &&
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
                if (tap_view &&
                    (abs(touch_dx) >= PLAYER_IPTV_SWIPE_MIN_PX ||
                     abs(touch_dy) >= PLAYER_IPTV_SWIPE_MIN_PX))
                {
                    touch_swipe = true;
                    touch_swipe_start_x = touch_trace.start_x;
                    touch_swipe_start_y = touch_trace.start_y;
                    touch_swipe_end_x = touch_trace.last_x;
                    touch_swipe_end_y = touch_trace.last_y;
                    main_input_trace("[input] touch swipe accepted start=%d,%d end=%d,%d duration_ms=%llu\n",
                                     (int)touch_swipe_start_x,
                                     (int)touch_swipe_start_y,
                                     (int)touch_swipe_end_x,
                                     (int)touch_swipe_end_y,
                                     (unsigned long long)touch_duration_ms);
                }
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
            bool player_ready = rendererPrestarted && videoRenderReady;
            bool skip_iptv_panel_input = false;
            bool iptv_touch_consumed = false;

            if (!iptv_panel_open &&
                touch_tap &&
                touch_tap_x >= 820 && touch_tap_x <= 1208 &&
                touch_tap_y >= 278 && touch_tap_y <= 580)
            {
                iptv_panel_open = true;
                iptv_sources_open = false;
                main_input_trace("[input] action=touch-iptv-panel open=1 x=%d y=%d\n",
                                 (int)touch_tap_x,
                                 (int)touch_tap_y);
            }

            if (iptv_panel_open && touch_swipe)
            {
                iptv_touch_consumed = main_iptv_handle_panel_touch_swipe((int)touch_swipe_start_x,
                                                                         (int)touch_swipe_start_y,
                                                                         (int)touch_swipe_end_x,
                                                                         (int)touch_swipe_end_y,
                                                                         iptv_sources_open);
            }
            if (iptv_panel_open && touch_tap)
            {
                iptv_touch_consumed = main_iptv_handle_panel_touch_tap((int)touch_tap_x,
                                                                       (int)touch_tap_y,
                                                                       player_ready,
                                                                       &iptv_panel_open,
                                                                       &iptv_sources_open) ||
                                      iptv_touch_consumed;
            }
            if (iptv_touch_consumed)
                touch_tap = false;

            if (!iptv_panel_open && (kDown & HidNpadButton_A) && home_state.playback_active)
            {
                bool ok = player_view_show_video();
                main_input_trace("[input] action=return-to-player ok=%d state=%s\n",
                                 ok ? 1 : 0,
                                 have_snapshot ? main_player_state_name(snapshot.state) : "none");
            }

            bool stick_open_panel = !iptv_panel_open &&
                                    (kDown & (HidNpadButton_StickL | HidNpadButton_StickR));
            if ((kDown & HidNpadButton_X) || stick_open_panel)
            {
                bool was_open = iptv_panel_open;
                if (!iptv_panel_open)
                {
                    iptv_panel_open = true;
                    iptv_sources_open = false;
                }
                else
                {
                    iptv_sources_open = !iptv_sources_open;
                }
                main_input_trace("[input] action=iptv-panel open=%d page=%s\n",
                                 iptv_panel_open ? 1 : 0,
                                 iptv_sources_open ? "sources" : "channels");
                if (!was_open)
                    skip_iptv_panel_input = true;
            }

            if (!iptv_panel_open && (kDown & HidNpadButton_Y))
            {
                bool ok = iptv_refresh_all_async();
                main_input_trace("[input] action=iptv-refresh-all queued=%d\n", ok ? 1 : 0);
            }

            if (!iptv_panel_open && (kDown & HidNpadButton_Minus))
            {
                MainIptvUrlResult result = main_iptv_prompt_and_open(player_ready);
                skip_iptv_panel_input = true;
                main_input_trace("[input] action=iptv-open-url result=%d\n", (int)result);
                if (result == MAIN_IPTV_URL_PLAYING)
                {
                    iptv_panel_open = false;
                    (void)player_view_show_video();
                }
                else if (result == MAIN_IPTV_URL_SOURCE_QUEUED)
                {
                    iptv_panel_open = true;
                    iptv_sources_open = false;
                }
            }

            if (iptv_panel_open && !skip_iptv_panel_input)
                main_iptv_handle_panel_input(kDown,
                                             kHeld,
                                             &pad,
                                             &iptv_repeater,
                                             player_ready,
                                             &iptv_panel_open,
                                             &iptv_sources_open);

            build_home_view_state(&home_state,
                                  storageReady,
                                  networkReady,
                                  &protocolStatus,
                                  videoRenderReady,
                                  iptv_panel_open,
                                  iptv_sources_open,
                                  have_snapshot ? &snapshot : NULL);
            if (videoPlatformReady)
                player_view_set_home_state(&home_state);

            bool rendered = videoPlatformReady && player_view_render_frame();
            if (!rendered && (!videoPlatformReady || !player_view_has_foreground_renderer()))
            {
                render_home_view(&home_state);
                consoleUpdate(NULL);
            }
        }
        else
        {
            bool player_ready = rendererPrestarted && videoRenderReady;
            bool iptv_menu_input = false;
            bool skip_iptv_menu_buttons = false;

            if (have_snapshot)
            {
                if (!snapshot.has_media)
                {
                    player_ui_clear(&video_ui);
                }
                else
                    player_ui_sync(&video_ui, &snapshot);

                bool stick_open_panel = !iptv_panel_open &&
                                        (kDown & (HidNpadButton_StickL | HidNpadButton_StickR));
                if (((kDown & HidNpadButton_X) || stick_open_panel) && home_state.iptv_channel_count > 1)
                {
                    bool was_open = iptv_panel_open;
                    if (!iptv_panel_open)
                    {
                        iptv_panel_open = true;
                        iptv_sources_open = false;
                    }
                    else
                    {
                        iptv_sources_open = !iptv_sources_open;
                    }
                    iptv_menu_input = true;
                    player_ui_hide_overlay(&video_ui);
                    touch_seek.active = false;
                    main_input_trace("[input] action=iptv-video-menu open=%d page=%s\n",
                                     iptv_panel_open ? 1 : 0,
                                     iptv_sources_open ? "sources" : "channels");
                    if (!was_open)
                        skip_iptv_menu_buttons = true;
                }
                else if (iptv_panel_open)
                {
                    iptv_menu_input = true;
                }

                if (iptv_panel_open && touch_swipe)
                {
                    (void)main_iptv_handle_panel_touch_swipe((int)touch_swipe_start_x,
                                                              (int)touch_swipe_start_y,
                                                              (int)touch_swipe_end_x,
                                                              (int)touch_swipe_end_y,
                                                              iptv_sources_open);
                }
                if (iptv_panel_open && touch_tap)
                {
                    (void)main_iptv_handle_panel_touch_tap((int)touch_tap_x,
                                                           (int)touch_tap_y,
                                                           player_ready,
                                                           &iptv_panel_open,
                                                           &iptv_sources_open);
                    touch_tap = false;
                }

                if (iptv_menu_input && !skip_iptv_menu_buttons)
                {
                    main_iptv_handle_panel_input(kDown,
                                                 kHeld,
                                                 &pad,
                                                 &iptv_repeater,
                                                 player_ready,
                                                 &iptv_panel_open,
                                                 &iptv_sources_open);
                }

                if (!iptv_menu_input && touch_tap)
                {
                    if (snapshot.has_media)
                    {
                        bool overlay_visible = player_ui_overlay_visible(&video_ui);
                        bool home_button_visible = overlay_visible ||
                                                   snapshot.state == PLAYER_STATE_LOADING ||
                                                   snapshot.state == PLAYER_STATE_BUFFERING ||
                                                   snapshot.state == PLAYER_STATE_SEEKING;
                        bool channels_button_visible = home_button_visible && home_state.iptv_channel_count > 1;
                        bool action_hints_hit = main_touch_video_action_hints_hit((int)touch_tap_x, (int)touch_tap_y);
                        bool home_button_hit = home_button_visible &&
                                               action_hints_hit &&
                                               ((channels_button_visible && touch_tap_x >= 1030 && touch_tap_x <= 1130) ||
                                                (!channels_button_visible && touch_tap_x >= 1140 && touch_tap_x <= 1279));
                        bool channels_button_hit = channels_button_visible &&
                                                   action_hints_hit &&
                                                   touch_tap_x >= 1131 && touch_tap_x <= 1279;
                        if (home_button_hit)
                        {
                            bool stop_dispatched = false;
                            bool ok = main_request_player_home(&snapshot,
                                                               input_now_ms,
                                                               &return_home_pending,
                                                               &return_home_ready_ms,
                                                               &stop_dispatched);
                            player_ui_hide_overlay(&video_ui);
                            touch_seek.active = false;
                            main_input_trace("[input] action=touch-home ok=%d stop=%d x=%d y=%d\n",
                                             ok ? 1 : 0,
                                             stop_dispatched ? 1 : 0,
                                             (int)touch_tap_x,
                                             (int)touch_tap_y);
                        }
                        else if (channels_button_hit)
                        {
                            iptv_panel_open = true;
                            iptv_sources_open = false;
                            player_ui_hide_overlay(&video_ui);
                            touch_seek.active = false;
                            main_input_trace("[input] action=touch-iptv-menu open=1 x=%d y=%d\n",
                                             (int)touch_tap_x,
                                             (int)touch_tap_y);
                        }
                        else if (overlay_visible && main_touch_center_button_hit((int)touch_tap_x, (int)touch_tap_y))
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

                if (!iptv_menu_input)
                {
                    padRepeaterUpdate(&video_repeater,
                                      kHeld & (HidNpadButton_L | HidNpadButton_R |
                                               HidNpadButton_Left | HidNpadButton_Right |
                                               HidNpadButton_Up | HidNpadButton_Down));
                    u64 repeated_video = padRepeaterGetButtons(&video_repeater);
                    u64 video_nav = kDown | repeated_video;

                    if (kDown & HidNpadButton_B)
                    {
                        bool stop_dispatched = false;
                        bool ok = main_request_player_home(&snapshot,
                                                           input_now_ms,
                                                           &return_home_pending,
                                                           &return_home_ready_ms,
                                                           &stop_dispatched);
                        player_ui_hide_overlay(&video_ui);
                        touch_seek.active = false;
                        main_input_trace("[input] action=return-home ok=%d stop=%d state=%s\n",
                                         ok ? 1 : 0,
                                         stop_dispatched ? 1 : 0,
                                         main_player_state_name(snapshot.state));
                    }

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
            }

            build_home_view_state(&home_state,
                                  storageReady,
                                  networkReady,
                                  &protocolStatus,
                                  videoRenderReady,
                                  iptv_panel_open,
                                  iptv_sources_open,
                                  have_snapshot ? &snapshot : NULL);
            if (videoPlatformReady)
                player_view_set_home_state(&home_state);

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
    shutdown_stdio_trace("[INFO] [shutdown] begin reason=%s network_ready=%d dlna_running=%d airplay_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
                         exit_reason_name(exit_reason),
                         networkReady ? 1 : 0,
                         protocolStatus.services[PROTOCOL_SERVICE_DLNA] ==
                                 PROTOCOL_SERVICE_RUNNING ? 1 : 0,
                         protocolStatus.airplay.running ? 1 : 0,
                         videoRenderReady ? 1 : 0,
                         g_nxlinkSock,
                         storageReady ? 1 : 0);
    log_info("[shutdown] begin reason=%s network_ready=%d dlna_running=%d airplay_running=%d video_ready=%d nxlink_fd=%d storage_ready=%d\n",
             exit_reason_name(exit_reason),
             networkReady ? 1 : 0,
             protocolStatus.services[PROTOCOL_SERVICE_DLNA] ==
                     PROTOCOL_SERVICE_RUNNING ? 1 : 0,
             protocolStatus.airplay.running ? 1 : 0,
             videoRenderReady ? 1 : 0,
             g_nxlinkSock,
             storageReady ? 1 : 0);

    shutdown_stdio_trace("[INFO] [shutdown] step=protocol_coordinator_stop begin state=%s owner=%s generation=%u\n",
                         protocol_coordinator_state_name(protocolStatus.state),
                         player_media_owner_name(protocolStatus.active_media.owner),
                         protocolStatus.active_media.generation);
    log_info("[shutdown] step=protocol_coordinator_stop begin state=%s owner=%s generation=%u\n",
             protocol_coordinator_state_name(protocolStatus.state),
             player_media_owner_name(protocolStatus.active_media.owner),
             protocolStatus.active_media.generation);
    protocol_coordinator_stop();
    (void)protocol_coordinator_get_snapshot(&protocolStatus);
    shutdown_stdio_trace("[INFO] [shutdown] step=protocol_coordinator_stop done state=%s\n",
                         protocol_coordinator_state_name(protocolStatus.state));
    log_info("[shutdown] step=protocol_coordinator_stop done state=%s\n",
             protocol_coordinator_state_name(protocolStatus.state));

    if (rendererPrestarted)
    {
        PlayerCommandRequest stop_request = {
            .kind = PLAYER_COMMAND_STOP_ANY,
            .source = PLAYER_COMMAND_SOURCE_APP,
        };
        PlayerCommandStatus stop_status;
        bool actor_idle;

        shutdown_stdio_trace("[INFO] [shutdown] step=player_stop begin\n");
        stop_status = player_submit_command_wait(&stop_request, 2000u);
        shutdown_stdio_trace("[INFO] [shutdown] step=player_stop done status=%s\n",
                             player_command_status_name(stop_status));
        log_info("[shutdown] step=player_quiesce begin\n");
        player_quiesce();
        actor_idle = player_wait_idle(2000u);
        if (!actor_idle)
        {
            PlayerRuntimeHealth health = {0};

            (void)player_get_runtime_health(&health);
            log_error("[shutdown] phase=player-drain stalled command=%llu command_age_ms=%llu heartbeat_age_ms=%llu queue=%zu\n",
                      (unsigned long long)health.current_command_id,
                      (unsigned long long)health.current_command_age_ms,
                      (unsigned long long)health.heartbeat_age_ms,
                      health.queue_depth);
            (void)player_wait_idle(UINT32_MAX);
        }
        log_info("[shutdown] step=player_quiesce done idle=1\n");
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

    if (rendererPrestarted)
    {
        shutdown_stdio_trace("[INFO] [shutdown] step=player_deinit begin\n");
        log_info("[shutdown] step=player_deinit begin\n");
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
