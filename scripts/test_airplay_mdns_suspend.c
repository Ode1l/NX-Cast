#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "app/network_diagnostics.h"
#include "protocol/airplay/discovery/mdns.h"

#define WAIT_STEP_MS 20u
#define WAIT_LIMIT_MS 2000u

static void sleep_milliseconds(unsigned milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };

    nanosleep(&duration, NULL);
}

static bool wait_for_phase(AirPlayMdnsPhase phase)
{
    AirPlayMdnsDiagnostics diagnostics;

    for (unsigned waited = 0u; waited < WAIT_LIMIT_MS; waited += WAIT_STEP_MS)
    {
        if (airplay_mdns_get_diagnostics(&diagnostics) && diagnostics.phase == phase)
            return true;
        sleep_milliseconds(WAIT_STEP_MS);
    }
    return false;
}

static bool wait_for_select_iteration(void)
{
    AirPlayMdnsDiagnostics diagnostics;

    for (unsigned waited = 0u; waited < WAIT_LIMIT_MS; waited += WAIT_STEP_MS)
    {
        if (airplay_mdns_get_diagnostics(&diagnostics) &&
            diagnostics.select_iterations > 0u)
            return true;
        sleep_milliseconds(WAIT_STEP_MS);
    }
    return false;
}

static bool wait_for_resume_announcement(uint64_t packets_sent_before_resume)
{
    AirPlayMdnsDiagnostics diagnostics;

    for (unsigned waited = 0u; waited < WAIT_LIMIT_MS; waited += WAIT_STEP_MS)
    {
        if (airplay_mdns_get_diagnostics(&diagnostics) &&
            !diagnostics.suspended &&
            diagnostics.phase != AIRPLAY_MDNS_PHASE_SUSPENDED &&
            diagnostics.packets_sent >= packets_sent_before_resume + 2u)
            return true;
        sleep_milliseconds(WAIT_STEP_MS);
    }
    return false;
}

int main(void)
{
    AirPlayMdnsConfig config = {0};
    AirPlayMdnsDiagnostics suspended;
    AirPlayMdnsDiagnostics after_wait;
    AirPlayMdnsDiagnostics stopped;
    NetworkDiagnosticSnapshot network_snapshot;

    if (!network_diagnostics_reset())
        return 1;

    config.friendly_name = "NX-Cast Suspend Test";
    config.control_port = 7000u;
    config.ipv4_address = inet_addr("127.0.0.1");
    config.features = AIRPLAY_MDNS_FEATURES_MIRROR_COMPAT;
    config.pin_required = true;
    snprintf(config.pairing_id, sizeof(config.pairing_id),
             "00112233-4455-4677-8899-aabbccddeeff");
    for (size_t index = 0u; index < sizeof(config.device_id); ++index)
        config.device_id[index] = (uint8_t)(0x10u + index);
    for (size_t index = 0u; index < sizeof(config.public_key); ++index)
        config.public_key[index] = (uint8_t)(0x80u + index);
    config.test_bind_port = 0u;
    config.test_announcement_port = 9u;
    config.test_announcement_address = inet_addr("127.0.0.1");
    config.test_skip_multicast_join = true;

    if (!airplay_mdns_start(&config) || !wait_for_select_iteration() ||
        !network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS,
                                          &network_snapshot) ||
        network_snapshot.open_sockets != 1u ||
        network_snapshot.operation_count == 0u)
        return 1;

    airplay_mdns_set_suspended(true);
    if (!airplay_mdns_is_suspended() ||
        !wait_for_phase(AIRPLAY_MDNS_PHASE_SUSPENDED) ||
        !airplay_mdns_get_diagnostics(&suspended))
        return 1;

    sleep_milliseconds(500u);
    if (!airplay_mdns_get_diagnostics(&after_wait) ||
        !after_wait.suspended ||
        after_wait.phase != AIRPLAY_MDNS_PHASE_SUSPENDED ||
        after_wait.select_iterations != suspended.select_iterations ||
        after_wait.packets_sent != suspended.packets_sent ||
        after_wait.worker_heartbeat_age_ms > 300u)
        return 1;

    airplay_mdns_set_suspended(false);
    if (airplay_mdns_is_suspended() ||
        !wait_for_resume_announcement(suspended.packets_sent))
        return 1;

    airplay_mdns_set_suspended(true);
    if (!wait_for_phase(AIRPLAY_MDNS_PHASE_SUSPENDED))
        return 1;
    airplay_mdns_stop();
    if (!airplay_mdns_get_diagnostics(&stopped) ||
        stopped.phase != AIRPLAY_MDNS_PHASE_STOPPED || stopped.running ||
        stopped.suspended || stopped.socket_open ||
        !network_diagnostics_get_snapshot(NETWORK_DIAGNOSTIC_MDNS,
                                          &network_snapshot) ||
        network_snapshot.open_sockets != 0u ||
        network_snapshot.active_operations != 0u ||
        network_snapshot.socket_close_underflows != 0u)
        return 1;

    puts("AirPlay mDNS suspend tests passed");
    return 0;
}
