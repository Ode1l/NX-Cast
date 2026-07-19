#ifndef NXCAST_AIRPLAY_SECURITY_PAIRING_H
#define NXCAST_AIRPLAY_SECURITY_PAIRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/airplay/airplay.h"
#include "protocol/airplay/protocol/rtsp.h"

typedef enum
{
    AIRPLAY_PAIRING_STATE_IDLE = 0,
    AIRPLAY_PAIRING_STATE_PIN_STARTED,
    AIRPLAY_PAIRING_STATE_SRP_CHALLENGE,
    AIRPLAY_PAIRING_STATE_SRP_VERIFIED,
    AIRPLAY_PAIRING_STATE_PAIRED,
    AIRPLAY_PAIRING_STATE_VERIFY_CHALLENGE,
    AIRPLAY_PAIRING_STATE_VERIFIED,
    AIRPLAY_PAIRING_STATE_FAILED
} AirPlayPairingState;

#if defined(AIRPLAY_TESTING)
typedef bool (*AirPlayPairingRandomCallback)(void *context, uint8_t *output, size_t size);
#endif

typedef struct
{
    const char *storage_directory;
    AirPlayPinDisplayCallback pin_display_callback;
    AirPlayPinDismissCallback pin_dismiss_callback;
    void *pin_user_data;
#if defined(AIRPLAY_TESTING)
    AirPlayPairingRandomCallback random_callback;
    void *random_context;
#endif
} AirPlayPairingConfig;

typedef struct AirPlayPairingService AirPlayPairingService;

bool airplay_pairing_service_create(const AirPlayPairingConfig *config,
                                    AirPlayPairingService **service_out);
void airplay_pairing_service_destroy(AirPlayPairingService *service);

bool airplay_pairing_route(AirPlayRtspSession *session,
                           const AirPlayRtspRequest *request,
                           AirPlayRtspResponse *response,
                           void *user_data);
void airplay_pairing_session_closed(AirPlayRtspSession *session, void *user_data);

AirPlayPairingState airplay_pairing_session_state(const AirPlayRtspSession *session);
bool airplay_pairing_session_verified(const AirPlayRtspSession *session);
bool airplay_pairing_session_shared_secret(const AirPlayRtspSession *session,
                                           uint8_t output[32]);
const char *airplay_pairing_state_name(AirPlayPairingState state);

#endif
