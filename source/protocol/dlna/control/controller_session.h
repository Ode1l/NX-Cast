#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint64_t last_poll_ms;
    unsigned poll_count;
    bool armed;
    bool timeout_consumed;
} DlnaControllerSessionSnapshot;

uint64_t dlna_controller_session_now_ms(void);
void dlna_controller_session_reset(void);
bool dlna_controller_session_note_poll_at(uint64_t now_ms);
void dlna_controller_session_refresh_at(uint64_t now_ms);
void dlna_controller_session_request_home(void);
bool dlna_controller_session_consume_home_request(void);
void dlna_controller_session_allow_home_retry(void);
bool dlna_controller_session_consume_timeout_at(uint64_t now_ms,
                                                uint64_t timeout_ms,
                                                bool eligible);
void dlna_controller_session_allow_timeout_retry(void);
bool dlna_controller_session_get_snapshot(
    DlnaControllerSessionSnapshot *snapshot_out);
