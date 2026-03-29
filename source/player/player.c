#include "player.h"

#include <stddef.h>
#include <string.h>

#include "log/log.h"
#include "player_backend.h"

static const PlayerBackendOps *g_backend = NULL;
static PlayerBackendType g_backend_type = PLAYER_BACKEND_AUTO;
static bool g_initialized = false;
static PlayerEventCallback g_event_callback = NULL;
static void *g_event_user = NULL;

static bool backend_available(const PlayerBackendOps *backend)
{
    if (!backend)
        return false;
    if (!backend->available)
        return true;
    return backend->available();
}

static const PlayerBackendOps *backend_ops_from_type(PlayerBackendType backend_type)
{
    switch (backend_type)
    {
    case PLAYER_BACKEND_MOCK:
        return &g_player_backend_mock;
    case PLAYER_BACKEND_LIBMPV:
        return &g_player_backend_libmpv;
    case PLAYER_BACKEND_AUTO:
    default:
        if (backend_available(&g_player_backend_libmpv))
            return &g_player_backend_libmpv;
        return &g_player_backend_mock;
    }
}

static const char *backend_name_from_type(PlayerBackendType backend_type)
{
    const PlayerBackendOps *backend = backend_ops_from_type(backend_type);
    if (!backend || !backend->name)
        return "unknown";
    return backend->name;
}

static void player_emit_from_backend(const PlayerEvent *event)
{
    if (!event)
        return;
    if (g_event_callback)
        g_event_callback(event, g_event_user);
}

bool player_set_backend(PlayerBackendType backend)
{
    if (g_initialized)
    {
        log_warn("[player] backend change rejected while initialized current=%s requested=%s\n",
                 player_get_backend_name(),
                 backend_name_from_type(backend));
        return false;
    }

    g_backend_type = backend;
    g_backend = NULL;
    log_info("[player] backend configured=%s\n", backend_name_from_type(backend));
    return true;
}

PlayerBackendType player_get_backend(void)
{
    return g_backend_type;
}

const char *player_get_backend_name(void)
{
    if (g_backend && g_backend->name)
        return g_backend->name;
    return backend_name_from_type(g_backend_type);
}

void player_set_event_callback(PlayerEventCallback callback, void *user)
{
    g_event_callback = callback;
    g_event_user = user;
}

bool player_init(void)
{
    if (g_initialized)
        return true;

    g_backend = backend_ops_from_type(g_backend_type);
    if (!g_backend)
    {
        log_error("[player] no backend selected\n");
        return false;
    }

    if (g_backend_type != PLAYER_BACKEND_AUTO && !backend_available(g_backend))
    {
        log_error("[player] backend unavailable name=%s\n", player_get_backend_name());
        g_backend = NULL;
        return false;
    }

    if (!backend_available(g_backend))
    {
        log_error("[player] auto backend resolution failed\n");
        g_backend = NULL;
        return false;
    }

    if (g_backend->set_event_sink)
        g_backend->set_event_sink(player_emit_from_backend);

    if (g_backend->init && !g_backend->init())
    {
        log_error("[player] backend init failed name=%s\n", player_get_backend_name());
        g_backend = NULL;
        return false;
    }

    g_initialized = true;
    log_info("[player] init backend=%s\n", player_get_backend_name());
    return true;
}

void player_deinit(void)
{
    if (!g_initialized)
        return;

    if (g_backend && g_backend->deinit)
        g_backend->deinit();

    log_info("[player] deinit backend=%s\n", player_get_backend_name());
    g_backend = NULL;
    g_initialized = false;
}

bool player_set_uri(const char *uri, const char *metadata)
{
    if (!g_initialized || !g_backend || !g_backend->set_uri)
        return false;
    return g_backend->set_uri(uri, metadata);
}

bool player_play(void)
{
    if (!g_initialized || !g_backend || !g_backend->play)
        return false;
    return g_backend->play();
}

bool player_pause(void)
{
    if (!g_initialized || !g_backend || !g_backend->pause)
        return false;
    return g_backend->pause();
}

bool player_stop(void)
{
    if (!g_initialized || !g_backend || !g_backend->stop)
        return false;
    return g_backend->stop();
}

bool player_seek_ms(int position_ms)
{
    if (!g_initialized || !g_backend || !g_backend->seek_ms)
        return false;
    return g_backend->seek_ms(position_ms);
}

bool player_set_volume(int volume_0_100)
{
    if (!g_initialized || !g_backend || !g_backend->set_volume)
        return false;
    return g_backend->set_volume(volume_0_100);
}

bool player_set_mute(bool mute)
{
    if (!g_initialized || !g_backend || !g_backend->set_mute)
        return false;
    return g_backend->set_mute(mute);
}

int player_get_position_ms(void)
{
    if (!g_initialized || !g_backend || !g_backend->get_position_ms)
        return 0;
    return g_backend->get_position_ms();
}

int player_get_duration_ms(void)
{
    if (!g_initialized || !g_backend || !g_backend->get_duration_ms)
        return 0;
    return g_backend->get_duration_ms();
}

int player_get_volume(void)
{
    if (!g_initialized || !g_backend || !g_backend->get_volume)
        return 0;
    return g_backend->get_volume();
}

bool player_get_mute(void)
{
    if (!g_initialized || !g_backend || !g_backend->get_mute)
        return false;
    return g_backend->get_mute();
}

PlayerState player_get_state(void)
{
    if (!g_initialized || !g_backend || !g_backend->get_state)
        return PLAYER_STATE_IDLE;
    return g_backend->get_state();
}
