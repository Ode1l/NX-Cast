#include "fairplay.h"

#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "playfair_decrypt.h"
#include "replies.h"

struct AirPlayFairPlay
{
    uint8_t stage2_request[AIRPLAY_FAIRPLAY_STAGE2_REQUEST_SIZE];
    bool stage2_complete;
};

static const uint8_t g_fairplay_magic[4] = {'F', 'P', 'L', 'Y'};

bool airplay_fairplay_is_available(void)
{
    return true;
}

bool airplay_fairplay_create(AirPlayFairPlay **session_out)
{
    AirPlayFairPlay *session;

    if (!session_out || *session_out)
        return false;
    session = calloc(1, sizeof(*session));
    if (!session)
        return false;
    *session_out = session;
    return true;
}

void airplay_fairplay_destroy(AirPlayFairPlay *session)
{
    if (!session)
        return;
    airplay_crypto_secure_zero(session, sizeof(*session));
    free(session);
}

static bool fairplay_header_valid(const uint8_t *request, size_t request_size)
{
    return request && request_size >= 5u &&
           memcmp(request, g_fairplay_magic, sizeof(g_fairplay_magic)) == 0 &&
           request[4] == 3u;
}

AirPlayFairPlayResult airplay_fairplay_setup(AirPlayFairPlay *session,
                                             const uint8_t *request,
                                             size_t request_size,
                                             uint8_t *response,
                                             size_t response_capacity,
                                             size_t *response_size)
{
    static const uint8_t stage2_header[12] = {
        'F', 'P', 'L', 'Y', 3u, 1u, 4u, 0u, 0u, 0u, 0u, 20u};

    if (!session || !request || !response || !response_size)
        return AIRPLAY_FAIRPLAY_INVALID_MESSAGE;
    *response_size = 0u;
    if (request_size == AIRPLAY_FAIRPLAY_STAGE1_REQUEST_SIZE ||
        request_size == AIRPLAY_FAIRPLAY_STAGE2_REQUEST_SIZE)
    {
        airplay_crypto_secure_zero(session->stage2_request,
                                   sizeof(session->stage2_request));
        session->stage2_complete = false;
    }
    if (!fairplay_header_valid(request, request_size))
        return AIRPLAY_FAIRPLAY_INVALID_MESSAGE;
    if (request_size == AIRPLAY_FAIRPLAY_STAGE1_REQUEST_SIZE)
    {
        if (response_capacity < AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE ||
            request[14] >= 4u || !playfair_copy_reply(request[14], response))
            return AIRPLAY_FAIRPLAY_INVALID_MESSAGE;
        *response_size = AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE;
        return AIRPLAY_FAIRPLAY_OK;
    }
    if (request_size != AIRPLAY_FAIRPLAY_STAGE2_REQUEST_SIZE ||
        response_capacity < AIRPLAY_FAIRPLAY_STAGE2_RESPONSE_SIZE ||
        request[12] >= 4u)
        return AIRPLAY_FAIRPLAY_INVALID_MESSAGE;
    memcpy(session->stage2_request, request, request_size);
    session->stage2_complete = true;
    memcpy(response, stage2_header, sizeof(stage2_header));
    memcpy(response + sizeof(stage2_header), request + 144u, 20u);
    *response_size = AIRPLAY_FAIRPLAY_STAGE2_RESPONSE_SIZE;
    return AIRPLAY_FAIRPLAY_OK;
}

AirPlayFairPlayResult airplay_fairplay_unwrap_key(
    AirPlayFairPlay *session,
    const uint8_t wrapped_key[AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE],
    uint8_t key_out[AIRPLAY_FAIRPLAY_AES_KEY_SIZE])
{
    uint8_t wrapped_copy[AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE];

    if (!session || !wrapped_key || !key_out)
        return AIRPLAY_FAIRPLAY_INVALID_MESSAGE;
    memset(key_out, 0, AIRPLAY_FAIRPLAY_AES_KEY_SIZE);
    if (!session->stage2_complete)
        return AIRPLAY_FAIRPLAY_INVALID_STATE;
    memcpy(wrapped_copy, wrapped_key, sizeof(wrapped_copy));
    playfair_decrypt(session->stage2_request, wrapped_copy, key_out);
    airplay_crypto_secure_zero(wrapped_copy, sizeof(wrapped_copy));
    return AIRPLAY_FAIRPLAY_OK;
}

const char *airplay_fairplay_result_name(AirPlayFairPlayResult result)
{
    switch (result)
    {
    case AIRPLAY_FAIRPLAY_OK:
        return "ok";
    case AIRPLAY_FAIRPLAY_UNSUPPORTED:
        return "unsupported";
    case AIRPLAY_FAIRPLAY_INVALID_MESSAGE:
        return "invalid-message";
    case AIRPLAY_FAIRPLAY_INVALID_STATE:
        return "invalid-state";
    default:
        return "unknown";
    }
}
