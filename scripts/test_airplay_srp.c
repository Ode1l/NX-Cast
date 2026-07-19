#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/srp.h"

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

typedef struct
{
    unsigned call_count;
} DeterministicRandom;

static bool deterministic_random(void *context, uint8_t *output, size_t size)
{
    DeterministicRandom *random = context;

    if (!random || !output)
        return false;
    if (random->call_count == 0 && size == AIRPLAY_SRP_SALT_SIZE)
    {
        for (size_t i = 0; i < size; ++i)
            output[i] = (uint8_t)i;
    }
    else if (random->call_count == 1 && size == 32)
    {
        for (size_t i = 0; i < size; ++i)
            output[i] = (uint8_t)(0x10u + i);
    }
    else
    {
        return false;
    }
    random->call_count++;
    return true;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

static bool decode_hex(const char *text, uint8_t *output, size_t size)
{
    if (!text || !output || strlen(text) != size * 2)
        return false;
    for (size_t i = 0; i < size; ++i)
    {
        int high = hex_digit(text[i * 2]);
        int low = hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static AirPlaySrpServer *create_server(void)
{
    DeterministicRandom random = {0};
    AirPlaySrpServer *server = NULL;

    CHECK(airplay_srp_server_create("test-client", "1234", deterministic_random,
                                    &random, &server));
    CHECK(random.call_count == 2);
    return server;
}

static void test_deterministic_transcript(void)
{
    static const char expected_b_hex[] =
        "7ec63636ea1c45f88f2b7e9a8740b96da93eeada71cfa9c71d4a0e94838c5a7f"
        "fdcac612ef7dc0db39e97bc0a4eebbfe551f09e7bab6a45d7b7d0cf6679040ba"
        "88381213d10d2ca2c05408a417f52aa6d61aa6ddfcef3d9f55478d5160456897"
        "735fcdb62779079c6ae3187bd6fcb56898ec8a6e3bc2c8877b2fbbd37eec2c4f"
        "2d26673e436d120530f5f23f1279a2f1e834157d4dd12d7894fe8f5d4b8fb2b"
        "33e6f931c2e42ecac41646d06c2955e25cf2414afbad1b5f3c2f360b9b486636"
        "61de3dec2305d358d6d060a1615097860cdbd78e1cf5d546431897df659aaacf3"
        "7d663c931f8462f462d5198b413e2b990c9c08779e796f8f9e5c6dc2b1d75222";
    static const char client_a_hex[] =
        "3dffa2dc33adbb1c49906deb5b17f3913df5da6a46c6f1d03c14c5cfd5b7d680"
        "2bf4890929be4108db554647e8d5d1a97e4ba011d08553112d673c97ddff540c"
        "c8c87650e7688390e2b4a951bec0c0b0e0d89e5cd339d311adb894dd6b797604"
        "bafe33ff2d118e9b3b4ae361f6b2c6b5cb53d7bd04c7ad9e560aa1b5bd67fa30"
        "c6e71abe9008b1c0a9ca8765dc6ae3a5e4f51e5b29874dc9dbf222e432e40391"
        "84a0660461a49d028b2974d0d44ccf7bd04466e59b82d327745ac6f217ae5f08"
        "b1d7cd4614a4e91960536367b751ce2df8c21dc59ad8d0b622a1d00eccda3e7b"
        "5c8fbbd787dbd5d6e36f0c5d3aa904752827ff8785b55ce64e70f9288f4caa49";
    uint8_t expected_salt[16];
    uint8_t expected_b[256];
    uint8_t client_a[256];
    uint8_t client_m[20];
    uint8_t expected_m2[20];
    uint8_t expected_key[40];
    uint8_t server_m2[20];
    uint8_t session_key[40];
    AirPlaySrpServer *server = create_server();

    CHECK(server != NULL);
    CHECK(decode_hex("800102030405060708090a0b0c0d0e0f",
                     expected_salt, sizeof(expected_salt)));
    CHECK(decode_hex(expected_b_hex, expected_b, sizeof(expected_b)));
    CHECK(decode_hex(client_a_hex, client_a, sizeof(client_a)));
    CHECK(decode_hex("8c4150f81731272f2cda897cea534ab6d17dd0c7",
                     client_m, sizeof(client_m)));
    CHECK(decode_hex("06560c5b08892523f1eb0591165f4efe9db2a999",
                     expected_m2, sizeof(expected_m2)));
    CHECK(decode_hex("797406aa9d798ef62caaab3ae17be0fe811db907"
                     "3072bb4f1f4438bf8bf0585b655559c890b16f5c",
                     expected_key, sizeof(expected_key)));
    CHECK(airplay_crypto_equal(airplay_srp_server_salt(server), expected_salt,
                               sizeof(expected_salt)));
    CHECK(airplay_crypto_equal(airplay_srp_server_public_key(server), expected_b,
                               sizeof(expected_b)));
    CHECK(airplay_srp_server_verify(server, client_a, sizeof(client_a), client_m,
                                    server_m2));
    CHECK(airplay_crypto_equal(server_m2, expected_m2, sizeof(server_m2)));
    CHECK(airplay_srp_server_session_key(server, session_key));
    CHECK(airplay_crypto_equal(session_key, expected_key, sizeof(session_key)));
    CHECK(!airplay_srp_server_verify(server, client_a, sizeof(client_a), client_m,
                                     server_m2));
    airplay_srp_server_destroy(server);
}

static void test_rejections(void)
{
    uint8_t client_a[256] = {0};
    uint8_t proof[20] = {0};
    uint8_t server_proof[20];
    AirPlaySrpServer *server = create_server();

    CHECK(server != NULL);
    CHECK(!airplay_srp_server_verify(server, client_a, sizeof(client_a), proof,
                                     server_proof));
    for (size_t i = 0; i < sizeof(server_proof); ++i)
        CHECK(server_proof[i] == 0);
    airplay_srp_server_destroy(server);

    server = create_server();
    client_a[255] = 2;
    CHECK(!airplay_srp_server_verify(server, client_a, sizeof(client_a), proof,
                                     server_proof));
    airplay_srp_server_destroy(server);
}

int main(void)
{
    test_deterministic_transcript();
    test_rejections();
    if (g_failures != 0)
    {
        fprintf(stderr, "AirPlay SRP tests failed: %d\n", g_failures);
        return 1;
    }
    puts("AirPlay SRP tests passed");
    return 0;
}
