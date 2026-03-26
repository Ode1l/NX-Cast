#include "player.h"

#include <stddef.h>

#include "player_backend.h"

static const PlayerBackendOps *g_backend = &g_player_backend_mock;
static bool g_initialized = false;
static PlayerEventCallback g_event_callback = NULL;
static void *g_event_user = NULL;

static void player_emit_from_backend(const PlayerEvent *event)
{
    if (!event)
        return;
    if (g_event_callback)
        g_event_callback(event, g_event_user);
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
    if (!g_backend)
        return false;

    if (g_backend->set_event_sink)
        g_backend->set_event_sink(player_emit_from_backend);

    if (g_backend->init && !g_backend->init())
        return false;

    g_initialized = true;
    return true;
}

void player_deinit(void)
{
    if (!g_initialized)
        return;

    if (g_backend && g_backend->deinit)
        g_backend->deinit();
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
