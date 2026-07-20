#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/fairplay.h"

static int g_failures;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

static bool decode_hex(const char *text, uint8_t *output, size_t output_size)
{
    if (!text || strlen(text) != output_size * 2u)
        return false;
    for (size_t index = 0u; index < output_size; ++index)
    {
        int high = hex_digit(text[index * 2u]);
        int low = hex_digit(text[index * 2u + 1u]);
        if (high < 0 || low < 0)
            return false;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void initialize_request(uint8_t *request, size_t request_size)
{
    for (size_t index = 0u; index < request_size; ++index)
        request[index] = (uint8_t)((index * 3u + 1u) & 0xffu);
    request[0] = 'F';
    request[1] = 'P';
    request[2] = 'L';
    request[3] = 'Y';
    request[4] = 3u;
    request[12] = 0u;
    request[14] = 0u;
}

static void test_stage1_responses(void)
{
    static const char *const expected_hashes[4] = {
        "8e1a11ea61e4c397f30480910786893aaebb5bff59bf2074db9d7e84fda49dbc",
        "e3730ce34481b93312c97ecbebe5b2128ee16a8e4d0140e89904ac9ff7509df3",
        "c6ab6f9488d29f4c7ec395981cdd12f14a5fa5c51ce61ae6ce43c570f7eae989",
        "8c53e03e2a08e23558e48287851d1cf39297039a7a60bf906a582e22c92f826b"};
    AirPlayFairPlay *session = NULL;
    uint8_t request[AIRPLAY_FAIRPLAY_STAGE1_REQUEST_SIZE] = {0};
    uint8_t response[AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE];
    uint8_t expected_hash[32];
    uint8_t actual_hash[32];
    size_t response_size;

    CHECK(airplay_fairplay_create(&session));
    initialize_request(request, sizeof(request));
    for (uint8_t mode = 0u; mode < 4u; ++mode)
    {
        request[14] = mode;
        memset(response, 0xa5, sizeof(response));
        response_size = 99u;
        CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                     sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_OK);
        CHECK(response_size == sizeof(response));
        CHECK(decode_hex(expected_hashes[mode], expected_hash, sizeof(expected_hash)));
        CHECK(airplay_crypto_sha256(response, sizeof(response), actual_hash));
        CHECK(airplay_crypto_equal(actual_hash, expected_hash, sizeof(actual_hash)));
    }

    request[14] = 4u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    request[14] = 0u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response) - 1u, &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    request[4] = 2u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    airplay_fairplay_destroy(session);
}

static void test_stage2_and_key_unwrap(void)
{
    static const uint8_t expected_key[AIRPLAY_FAIRPLAY_AES_KEY_SIZE] = {
        0x7du, 0xcau, 0x93u, 0xa1u, 0x6eu, 0xf6u, 0x44u, 0x63u,
        0x06u, 0xa2u, 0xd4u, 0x07u, 0x2du, 0x95u, 0x6bu, 0x00u};
    static const uint8_t stage2_header[12] = {
        'F', 'P', 'L', 'Y', 3u, 1u, 4u, 0u, 0u, 0u, 0u, 20u};
    AirPlayFairPlay *session = NULL;
    uint8_t request[AIRPLAY_FAIRPLAY_STAGE2_REQUEST_SIZE];
    uint8_t response[AIRPLAY_FAIRPLAY_STAGE2_RESPONSE_SIZE];
    uint8_t wrapped[AIRPLAY_FAIRPLAY_WRAPPED_KEY_SIZE];
    uint8_t key[AIRPLAY_FAIRPLAY_AES_KEY_SIZE];
    uint8_t stage1[AIRPLAY_FAIRPLAY_STAGE1_REQUEST_SIZE] = {0};
    uint8_t stage1_response[AIRPLAY_FAIRPLAY_STAGE1_RESPONSE_SIZE];
    size_t response_size = 0u;

    CHECK(airplay_fairplay_create(&session));
    CHECK(airplay_fairplay_is_available());
    initialize_request(request, sizeof(request));
    for (size_t index = 0u; index < sizeof(wrapped); ++index)
        wrapped[index] = (uint8_t)((index * 5u + 7u) & 0xffu);

    memset(key, 0xa5, sizeof(key));
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_INVALID_STATE);
    for (size_t index = 0u; index < sizeof(key); ++index)
        CHECK(key[index] == 0u);

    request[12] = 4u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    CHECK(response_size == 0u);
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_INVALID_STATE);
    request[12] = 0u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response) - 1u, &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    CHECK(response_size == 0u);

    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_OK);
    CHECK(response_size == sizeof(response));
    CHECK(memcmp(response, stage2_header, sizeof(stage2_header)) == 0);
    CHECK(memcmp(response + sizeof(stage2_header), request + 144u, 20u) == 0);
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_OK);
    CHECK(airplay_crypto_equal(key, expected_key, sizeof(key)));

    request[12] = 4u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_INVALID_STATE);
    request[12] = 0u;
    CHECK(airplay_fairplay_setup(session, request, sizeof(request), response,
                                 sizeof(response), &response_size) == AIRPLAY_FAIRPLAY_OK);

    initialize_request(stage1, sizeof(stage1));
    stage1[14] = 4u;
    CHECK(airplay_fairplay_setup(session, stage1, sizeof(stage1), stage1_response,
                                 sizeof(stage1_response), &response_size) == AIRPLAY_FAIRPLAY_INVALID_MESSAGE);
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_INVALID_STATE);
    stage1[14] = 0u;
    CHECK(airplay_fairplay_setup(session, stage1, sizeof(stage1), stage1_response,
                                 sizeof(stage1_response), &response_size) == AIRPLAY_FAIRPLAY_OK);
    CHECK(airplay_fairplay_unwrap_key(session, wrapped, key) == AIRPLAY_FAIRPLAY_INVALID_STATE);
    airplay_fairplay_destroy(session);
}

int main(void)
{
    test_stage1_responses();
    test_stage2_and_key_unwrap();
    if (g_failures != 0)
        return 1;
    puts("AirPlay FairPlay compatibility checks passed");
    return 0;
}
