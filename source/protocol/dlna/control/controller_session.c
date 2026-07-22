#include "controller_session.h"

#include <stdatomic.h>
#include <time.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#define DLNA_CONTROLLER_ARM_POLL_COUNT 4u
#define DLNA_CONTROLLER_ARM_WINDOW_MS 3000u

static atomic_uint_fast64_t g_last_poll_ms;
static atomic_uint g_poll_count;
static atomic_bool g_armed;
static atomic_bool g_timeout_consumed;
static atomic_bool g_home_requested;

static void controller_session_reset_tracking(void)
{
    atomic_store_explicit(&g_timeout_consumed, false, memory_order_release);
    atomic_store_explicit(&g_armed, false, memory_order_release);
    atomic_store_explicit(&g_poll_count, 0u, memory_order_release);
    atomic_store_explicit(&g_last_poll_ms, 0u, memory_order_release);
}

uint64_t dlna_controller_session_now_ms(void)
{
#ifdef __SWITCH__
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
#else
    struct timespec now = {0};

    if (timespec_get(&now, TIME_UTC) != TIME_UTC)
        return 0u;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
#endif
}

void dlna_controller_session_reset(void)
{
    controller_session_reset_tracking();
    atomic_store_explicit(&g_home_requested, false, memory_order_release);
}

bool dlna_controller_session_note_poll_at(uint64_t now_ms)
{
    uint64_t previous_ms;
    unsigned poll_count;
    bool expected = false;

    if (atomic_load_explicit(&g_armed, memory_order_acquire))
    {
        atomic_store_explicit(&g_last_poll_ms, now_ms, memory_order_release);
        return false;
    }

    previous_ms = atomic_load_explicit(&g_last_poll_ms,
                                       memory_order_acquire);
    poll_count = atomic_load_explicit(&g_poll_count, memory_order_acquire);
    if (poll_count > 0u && now_ms >= previous_ms &&
        now_ms - previous_ms <= DLNA_CONTROLLER_ARM_WINDOW_MS)
        ++poll_count;
    else
        poll_count = 1u;

    atomic_store_explicit(&g_last_poll_ms, now_ms, memory_order_release);
    atomic_store_explicit(&g_poll_count, poll_count, memory_order_release);
    if (poll_count < DLNA_CONTROLLER_ARM_POLL_COUNT)
        return false;

    if (!atomic_compare_exchange_strong_explicit(
            &g_armed, &expected, true, memory_order_acq_rel,
            memory_order_acquire))
        return false;
    atomic_store_explicit(&g_timeout_consumed, false, memory_order_release);
    return true;
}

void dlna_controller_session_refresh_at(uint64_t now_ms)
{
    if (atomic_load_explicit(&g_armed, memory_order_acquire))
        atomic_store_explicit(&g_last_poll_ms, now_ms, memory_order_release);
}

void dlna_controller_session_request_home(void)
{
    controller_session_reset_tracking();
    atomic_store_explicit(&g_home_requested, true, memory_order_release);
}

bool dlna_controller_session_consume_home_request(void)
{
    bool expected = true;

    return atomic_compare_exchange_strong_explicit(
        &g_home_requested, &expected, false, memory_order_acq_rel,
        memory_order_acquire);
}

void dlna_controller_session_allow_home_retry(void)
{
    atomic_store_explicit(&g_home_requested, true, memory_order_release);
}

bool dlna_controller_session_consume_timeout_at(uint64_t now_ms,
                                                uint64_t timeout_ms,
                                                bool eligible)
{
    uint64_t last_poll_ms;
    bool expected = false;

    if (!eligible)
    {
        controller_session_reset_tracking();
        return false;
    }
    if (timeout_ms == 0u ||
        !atomic_load_explicit(&g_armed, memory_order_acquire))
        return false;

    last_poll_ms = atomic_load_explicit(&g_last_poll_ms,
                                        memory_order_acquire);
    if (now_ms < last_poll_ms || now_ms - last_poll_ms < timeout_ms)
        return false;
    return atomic_compare_exchange_strong_explicit(
        &g_timeout_consumed, &expected, true, memory_order_acq_rel,
        memory_order_acquire);
}

void dlna_controller_session_allow_timeout_retry(void)
{
    if (atomic_load_explicit(&g_armed, memory_order_acquire))
        atomic_store_explicit(&g_timeout_consumed, false,
                              memory_order_release);
}

bool dlna_controller_session_get_snapshot(
    DlnaControllerSessionSnapshot *snapshot_out)
{
    if (!snapshot_out)
        return false;
    snapshot_out->last_poll_ms = atomic_load_explicit(
        &g_last_poll_ms, memory_order_acquire);
    snapshot_out->poll_count = atomic_load_explicit(
        &g_poll_count, memory_order_acquire);
    snapshot_out->armed = atomic_load_explicit(&g_armed,
                                               memory_order_acquire);
    snapshot_out->timeout_consumed = atomic_load_explicit(
        &g_timeout_consumed, memory_order_acquire);
    return true;
}
