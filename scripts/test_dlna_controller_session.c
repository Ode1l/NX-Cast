#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "protocol/dlna/control/controller_session.h"

static void establish_polling(uint64_t first_ms)
{
    assert(!dlna_controller_session_note_poll_at(first_ms));
    assert(!dlna_controller_session_note_poll_at(first_ms + 500u));
    assert(!dlna_controller_session_note_poll_at(first_ms + 1000u));
    assert(dlna_controller_session_note_poll_at(first_ms + 1500u));
}

static void test_established_session_expires_once(void)
{
    DlnaControllerSessionSnapshot snapshot = {0};

    dlna_controller_session_reset();
    establish_polling(100u);
    assert(dlna_controller_session_get_snapshot(&snapshot));
    assert(snapshot.armed && snapshot.poll_count == 4u);
    assert(snapshot.last_poll_ms == 1600u);
    assert(!dlna_controller_session_consume_timeout_at(
        11599u, 10000u, true));
    assert(dlna_controller_session_consume_timeout_at(
        11600u, 10000u, true));
    assert(!dlna_controller_session_consume_timeout_at(
        21600u, 10000u, true));

    dlna_controller_session_allow_timeout_retry();
    assert(dlna_controller_session_consume_timeout_at(
        21600u, 10000u, true));
}

static void test_poll_and_command_refresh_deadline(void)
{
    dlna_controller_session_reset();
    establish_polling(1000u);
    assert(!dlna_controller_session_note_poll_at(9000u));
    assert(!dlna_controller_session_consume_timeout_at(
        11999u, 10000u, true));
    dlna_controller_session_refresh_at(12000u);
    assert(!dlna_controller_session_consume_timeout_at(
        21999u, 10000u, true));
    assert(dlna_controller_session_consume_timeout_at(
        22000u, 10000u, true));
}

static void test_sparse_or_ineligible_session_never_expires(void)
{
    DlnaControllerSessionSnapshot snapshot = {0};

    dlna_controller_session_reset();
    assert(!dlna_controller_session_note_poll_at(100u));
    assert(!dlna_controller_session_note_poll_at(4100u));
    assert(!dlna_controller_session_note_poll_at(8100u));
    assert(!dlna_controller_session_note_poll_at(12100u));
    assert(dlna_controller_session_get_snapshot(&snapshot));
    assert(!snapshot.armed && snapshot.poll_count == 1u);
    assert(!dlna_controller_session_consume_timeout_at(
        50000u, 10000u, true));

    dlna_controller_session_reset();
    establish_polling(200u);
    assert(!dlna_controller_session_consume_timeout_at(
        20000u, 10000u, false));
    assert(dlna_controller_session_get_snapshot(&snapshot));
    assert(!snapshot.armed && snapshot.poll_count == 0u);
}

static void test_explicit_stop_requests_home_once(void)
{
    dlna_controller_session_reset();
    establish_polling(300u);
    dlna_controller_session_request_home();
    assert(dlna_controller_session_consume_home_request());
    assert(!dlna_controller_session_consume_home_request());

    dlna_controller_session_allow_home_retry();
    assert(dlna_controller_session_consume_home_request());
    assert(!dlna_controller_session_consume_timeout_at(
        50000u, 10000u, true));
}

int main(void)
{
    test_established_session_expires_once();
    test_poll_and_command_refresh_deadline();
    test_sparse_or_ineligible_session_never_expires();
    test_explicit_stop_requests_home_once();
    puts("DLNA controller session tests passed");
    return 0;
}
