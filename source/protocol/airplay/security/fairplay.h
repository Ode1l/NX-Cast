#ifndef NXCAST_AIRPLAY_SECURITY_FAIRPLAY_H
#define NXCAST_AIRPLAY_SECURITY_FAIRPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_FAIRPLAY_STAGE1_REQUEST_SIZE 16u
#define AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE 142u
#define AIRPLAY_FAIRPLAY_STAGE2_REQUEST_SIZE 164u
#define AIRPLAY_FAIRPLAY_STAGE2_RESPONSE_SIZE 32u
#define AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE 72u
#define AIRPLAY_FAIRPLAY_AES_KEY_SIZE 16u

typedef enum
{
    AIRPLAY_FAIRPLAY_OK = 0,
    AIRPLAY_FAIRPLAY_UNSUPPORTED,
    AIRPLAY_FAIRPLAY_INVALID_MESSAGE,
    AIRPLAY_FAIRPLAY_INVALID_STATE
} AirPlayFairPlayResult;

typedef struct AirPlayFairPlay AirPlayFairPlay;

bool airplay_fairplay_create(AirPlayFairPlay **session_out);
void airplay_fairplay_destroy(AirPlayFairPlay *session);
AirPlayFairPlayResult airplay_fairplay_setup(AirPlayFairPlay *session,
                                             const uint8_t *request,
                                             size_t request_size,
                                             uint8_t *response,
                                             size_t response_capacity,
                                             size_t *response_size);
AirPlayFairPlayResult airplay_fairplay_unwrap_key(
    AirPlayFairPlay *session,
    const uint8_t wrapped_key[AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE],
    uint8_t key_out[AIRPLAY_FAIRPLAY_AES_KEY_SIZE]);
const char *airplay_fairplay_result_name(AirPlayFairPlayResult result);

#endif
