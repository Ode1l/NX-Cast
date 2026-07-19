#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "protocol/airplay/protocol/plist.h"
#include "protocol/airplay/protocol/rtsp.h"
#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/pairing.h"

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
    unsigned display_count;
    unsigned dismiss_count;
    char pin[5];
} PinRecorder;

static void record_pin(const char pin[5], void *context)
{
    PinRecorder *recorder = context;
    recorder->display_count++;
    memcpy(recorder->pin, pin, sizeof(recorder->pin));
}

static void dismiss_pin(void *context)
{
    PinRecorder *recorder = context;
    recorder->dismiss_count++;
}

static bool deterministic_pairing_random(void *context, uint8_t *output, size_t size)
{
    (void)context;
    if (size == 2)
    {
        output[0] = 0x04;
        output[1] = 0xd1;
        return true;
    }
    if (size == 16)
    {
        for (size_t i = 0; i < size; ++i)
            output[i] = (uint8_t)i;
        return true;
    }
    if (size == 32)
    {
        for (size_t i = 0; i < size; ++i)
            output[i] = (uint8_t)(0x10u + i);
        return true;
    }
    return false;
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

static bool dispatch(AirPlayPairingService *service,
                     AirPlayRtspSession *session,
                     const char *method, const char *uri,
                     const uint8_t *body, size_t body_size,
                     const char *content_type,
                     AirPlayRtspResponse *response)
{
    AirPlayRtspRequest request = {0};

    snprintf(request.method, sizeof(request.method), "%s", method);
    snprintf(request.uri, sizeof(request.uri), "%s", uri);
    snprintf(request.protocol, sizeof(request.protocol), "RTSP/1.0");
    request.has_cseq = true;
    request.cseq = session->request_count + 1u;
    request.body = (uint8_t *)body;
    request.body_length = body_size;
    if (content_type)
    {
        snprintf(request.headers[0].name, sizeof(request.headers[0].name), "Content-Type");
        snprintf(request.headers[0].value, sizeof(request.headers[0].value), "%s", content_type);
        request.header_count = 1;
    }
    return airplay_rtsp_dispatch(session, &request, airplay_pairing_route,
                                 service, response);
}

static bool encode_dict(AirPlayPlistValue *root, uint8_t **body, size_t *body_size)
{
    AirPlayPlistError error;
    bool ok = airplay_plist_encode(root, body, body_size, &error);
    airplay_plist_free(root);
    return ok;
}

static bool dict_set_string(AirPlayPlistValue *dict, const char *key, const char *text)
{
    AirPlayPlistValue *value = airplay_plist_new_string(text);
    if (!value)
        return false;
    if (!airplay_plist_dict_set(dict, key, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static bool dict_set_data(AirPlayPlistValue *dict, const char *key,
                          const uint8_t *data, size_t size)
{
    AirPlayPlistValue *value = airplay_plist_new_data(data, size);
    if (!value)
        return false;
    if (!airplay_plist_dict_set(dict, key, value))
    {
        airplay_plist_free(value);
        return false;
    }
    return true;
}

static AirPlayPlistValue *decode_response(const AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    CHECK(airplay_plist_decode(response->body, response->body_length, &root, &error));
    return root;
}

static void derive_label(const char *label, const uint8_t *secret, size_t secret_size,
                         uint8_t *output, size_t output_size)
{
    uint8_t input[96];
    uint8_t hash[64];
    size_t label_size = strlen(label);

    memcpy(input, label, label_size);
    memcpy(input + label_size, secret, secret_size);
    CHECK(airplay_crypto_sha512(input, label_size + secret_size, hash));
    memcpy(output, hash, output_size);
    airplay_crypto_secure_zero(input, sizeof(input));
    airplay_crypto_secure_zero(hash, sizeof(hash));
}

static bool start_and_challenge(AirPlayPairingService *service,
                                AirPlayRtspSession *session,
                                PinRecorder *pin,
                                AirPlayRtspResponse *response)
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
    uint8_t expected_b[256];
    uint8_t expected_salt[16];
    uint8_t *body = NULL;
    size_t body_size = 0;
    size_t value_size;
    const uint8_t *value;
    AirPlayPlistValue *request = NULL;
    AirPlayPlistValue *root = NULL;

    if (!dispatch(service, session, "POST", "/pair-pin-start", NULL, 0, NULL, response))
        return false;
    CHECK(response->status_code == 200);
    CHECK(pin->display_count > 0);
    CHECK(strcmp(pin->pin, "1234") == 0);
    airplay_rtsp_response_clear(response);

    request = airplay_plist_new_dict();
    CHECK(request && dict_set_string(request, "method", "pin") &&
          dict_set_string(request, "user", "test-client") &&
          encode_dict(request, &body, &body_size));
    if (!dispatch(service, session, "POST", "/pair-setup-pin", body, body_size,
                  "application/x-apple-binary-plist", response))
        return false;
    airplay_plist_buffer_free(body);
    CHECK(response->status_code == 200);
    root = decode_response(response);
    CHECK(root != NULL);
    value = airplay_plist_get_data(airplay_plist_dict_get(root, "salt"), &value_size);
    CHECK(decode_hex("800102030405060708090a0b0c0d0e0f",
                     expected_salt, sizeof(expected_salt)));
    CHECK(value && value_size == sizeof(expected_salt) &&
          airplay_crypto_equal(value, expected_salt, sizeof(expected_salt)));
    value = airplay_plist_get_data(airplay_plist_dict_get(root, "pk"), &value_size);
    CHECK(decode_hex(expected_b_hex, expected_b, sizeof(expected_b)));
    CHECK(value && value_size == sizeof(expected_b) &&
          airplay_crypto_equal(value, expected_b, sizeof(expected_b)));
    airplay_plist_free(root);
    airplay_rtsp_response_clear(response);
    return true;
}

static bool send_srp_proof(AirPlayPairingService *service,
                           AirPlayRtspSession *session,
                           bool valid,
                           AirPlayRtspResponse *response)
{
    static const char client_a_hex[] =
        "3dffa2dc33adbb1c49906deb5b17f3913df5da6a46c6f1d03c14c5cfd5b7d680"
        "2bf4890929be4108db554647e8d5d1a97e4ba011d08553112d673c97ddff540c"
        "c8c87650e7688390e2b4a951bec0c0b0e0d89e5cd339d311adb894dd6b797604"
        "bafe33ff2d118e9b3b4ae361f6b2c6b5cb53d7bd04c7ad9e560aa1b5bd67fa30"
        "c6e71abe9008b1c0a9ca8765dc6ae3a5e4f51e5b29874dc9dbf222e432e40391"
        "84a0660461a49d028b2974d0d44ccf7bd04466e59b82d327745ac6f217ae5f08"
        "b1d7cd4614a4e91960536367b751ce2df8c21dc59ad8d0b622a1d00eccda3e7b"
        "5c8fbbd787dbd5d6e36f0c5d3aa904752827ff8785b55ce64e70f9288f4caa49";
    uint8_t client_a[256];
    uint8_t proof[20];
    uint8_t expected_server_proof[20];
    uint8_t *body = NULL;
    size_t body_size = 0;
    size_t value_size;
    const uint8_t *value;
    AirPlayPlistValue *request = airplay_plist_new_dict();
    AirPlayPlistValue *root;

    CHECK(decode_hex(client_a_hex, client_a, sizeof(client_a)));
    CHECK(decode_hex("8c4150f81731272f2cda897cea534ab6d17dd0c7", proof, sizeof(proof)));
    CHECK(decode_hex("06560c5b08892523f1eb0591165f4efe9db2a999",
                     expected_server_proof, sizeof(expected_server_proof)));
    if (!valid)
        proof[0] ^= 1u;
    CHECK(request && dict_set_data(request, "pk", client_a, sizeof(client_a)) &&
          dict_set_data(request, "proof", proof, sizeof(proof)) &&
          encode_dict(request, &body, &body_size));
    CHECK(dispatch(service, session, "POST", "/pair-setup-pin", body, body_size,
                   "application/x-apple-binary-plist", response));
    airplay_plist_buffer_free(body);
    if (!valid)
        return response->status_code == 470;
    CHECK(response->status_code == 200);
    root = decode_response(response);
    value = airplay_plist_get_data(airplay_plist_dict_get(root, "proof"), &value_size);
    CHECK(value && value_size == sizeof(expected_server_proof) &&
          airplay_crypto_equal(value, expected_server_proof,
                               sizeof(expected_server_proof)));
    airplay_plist_free(root);
    airplay_rtsp_response_clear(response);
    return true;
}

static bool complete_pair_setup(AirPlayPairingService *service,
                                AirPlayRtspSession *session,
                                uint8_t client_seed[32],
                                uint8_t client_public[32],
                                uint8_t server_public[32],
                                AirPlayRtspResponse *response)
{
    uint8_t session_key[40];
    uint8_t key[16];
    uint8_t nonce[16];
    uint8_t encrypted[32];
    uint8_t tag[16];
    uint8_t *body = NULL;
    size_t body_size = 0;
    size_t value_size;
    const uint8_t *value;
    AirPlayPlistValue *request;
    AirPlayPlistValue *root;

    CHECK(decode_hex("9d61b19deffd5a60ba844af492ec2cc4"
                     "4449c5697b326919703bac031cae7f60", client_seed, 32));
    CHECK(airplay_crypto_ed25519_public(client_seed, client_public));
    CHECK(decode_hex("797406aa9d798ef62caaab3ae17be0fe811db907"
                     "3072bb4f1f4438bf8bf0585b655559c890b16f5c",
                     session_key, sizeof(session_key)));
    derive_label("Pair-Setup-AES-Key", session_key, sizeof(session_key), key, sizeof(key));
    derive_label("Pair-Setup-AES-IV", session_key, sizeof(session_key), nonce, sizeof(nonce));
    nonce[15]++;
    CHECK(airplay_crypto_aes_gcm_encrypt(key, nonce, sizeof(nonce), NULL, 0,
                                         client_public, 32, encrypted, tag));
    request = airplay_plist_new_dict();
    CHECK(request && dict_set_data(request, "epk", encrypted, sizeof(encrypted)) &&
          dict_set_data(request, "authTag", tag, sizeof(tag)) &&
          encode_dict(request, &body, &body_size));
    CHECK(dispatch(service, session, "POST", "/pair-setup-pin", body, body_size,
                   "application/x-apple-binary-plist", response));
    airplay_plist_buffer_free(body);
    CHECK(response->status_code == 200);
    root = decode_response(response);
    value = airplay_plist_get_data(airplay_plist_dict_get(root, "epk"), &value_size);
    CHECK(value && value_size == 32);
    if (value && value_size == 32)
        memcpy(encrypted, value, 32);
    value = airplay_plist_get_data(airplay_plist_dict_get(root, "authTag"), &value_size);
    CHECK(value && value_size == 16);
    if (value && value_size == 16)
        memcpy(tag, value, 16);
    nonce[15]++;
    CHECK(airplay_crypto_aes_gcm_decrypt(key, nonce, sizeof(nonce), NULL, 0, tag,
                                         encrypted, sizeof(encrypted), server_public));
    airplay_plist_free(root);
    airplay_rtsp_response_clear(response);
    airplay_crypto_secure_zero(session_key, sizeof(session_key));
    return true;
}

static bool verify_pairing(AirPlayPairingService *service,
                           AirPlayRtspSession *session,
                           const uint8_t client_seed[32],
                           const uint8_t client_public[32],
                           const uint8_t server_public[32],
                           AirPlayRtspResponse *response)
{
    uint8_t client_x_private[32];
    uint8_t client_x_public[32];
    uint8_t request_body[68] = {0};
    uint8_t server_x_public[32];
    uint8_t shared[32];
    uint8_t key[16];
    uint8_t nonce[16];
    uint8_t signature[64];
    uint8_t message[64];
    uint8_t discard_in[64] = {0};
    uint8_t discard_out[64];
    AirPlayCryptoAesCtr aes = {0};
    AirPlayCryptoRng rng = {0};

    CHECK(decode_hex("77076d0a7318a57d3c16c17251b26645"
                     "df4c2f87ebc0992ab177fba51db92c2a",
                     client_x_private, sizeof(client_x_private)));
    CHECK(airplay_crypto_rng_init(&rng, "pairing client test"));
    CHECK(airplay_crypto_x25519_public(&rng, client_x_private, client_x_public));
    request_body[0] = 1;
    memcpy(request_body + 4, client_x_public, 32);
    memcpy(request_body + 36, client_public, 32);
    CHECK(dispatch(service, session, "POST", "/pair-verify", request_body,
                   sizeof(request_body), "application/octet-stream", response));
    CHECK(response->status_code == 200 && response->body_length == 96);
    memcpy(server_x_public, response->body, 32);
    CHECK(airplay_crypto_x25519_shared(&rng, client_x_private, server_x_public, shared));
    derive_label("Pair-Verify-AES-Key", shared, sizeof(shared), key, sizeof(key));
    derive_label("Pair-Verify-AES-IV", shared, sizeof(shared), nonce, sizeof(nonce));
    CHECK(airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), nonce));
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, response->body + 32, signature,
                                       sizeof(signature)));
    airplay_crypto_aes_ctr_deinit(&aes);
    memcpy(message, server_x_public, 32);
    memcpy(message + 32, client_x_public, 32);
    CHECK(airplay_crypto_ed25519_verify(server_public, message, sizeof(message), signature));
    airplay_rtsp_response_clear(response);

    memcpy(message, client_x_public, 32);
    memcpy(message + 32, server_x_public, 32);
    CHECK(airplay_crypto_ed25519_sign(client_seed, message, sizeof(message), signature));
    CHECK(airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), nonce));
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, discard_in, discard_out, sizeof(discard_out)));
    request_body[0] = 0;
    CHECK(airplay_crypto_aes_ctr_crypt(&aes, signature, request_body + 4, sizeof(signature)));
    airplay_crypto_aes_ctr_deinit(&aes);
    CHECK(dispatch(service, session, "POST", "/pair-verify", request_body,
                   sizeof(request_body), "application/octet-stream", response));
    CHECK(response->status_code == 200);
    CHECK(airplay_pairing_session_verified(session));
    CHECK(airplay_pairing_session_state(session) == AIRPLAY_PAIRING_STATE_VERIFIED);
    airplay_rtsp_response_clear(response);
    airplay_crypto_rng_deinit(&rng);
    return true;
}

static void test_pairing_flow_and_persistence(const char *directory)
{
    PinRecorder pin = {0};
    AirPlayPairingConfig config = {
        .storage_directory = directory,
        .pin_display_callback = record_pin,
        .pin_dismiss_callback = dismiss_pin,
        .pin_user_data = &pin,
        .random_callback = deterministic_pairing_random,
        .random_context = NULL};
    AirPlayPairingService *service = NULL;
    AirPlayRtspSession session;
    AirPlayRtspResponse response = {0};
    uint8_t client_seed[32];
    uint8_t client_public[32];
    uint8_t server_public[32];

    CHECK(airplay_pairing_service_create(&config, &service));
    airplay_rtsp_session_init(&session, 1);
    CHECK(start_and_challenge(service, &session, &pin, &response));
    CHECK(pin.dismiss_count == 1);
    CHECK(send_srp_proof(service, &session, true, &response));
    CHECK(complete_pair_setup(service, &session, client_seed, client_public,
                              server_public, &response));
    CHECK(airplay_pairing_session_state(&session) == AIRPLAY_PAIRING_STATE_PAIRED);
    CHECK(verify_pairing(service, &session, client_seed, client_public,
                         server_public, &response));
    airplay_pairing_session_closed(&session, service);
    airplay_pairing_service_destroy(service);

    service = NULL;
    CHECK(airplay_pairing_service_create(&config, &service));
    airplay_rtsp_session_init(&session, 2);
    CHECK(verify_pairing(service, &session, client_seed, client_public,
                         server_public, &response));
    airplay_pairing_session_closed(&session, service);
    airplay_pairing_service_destroy(service);
}

static void test_failures(const char *directory)
{
    PinRecorder pin = {0};
    AirPlayPairingConfig config = {
        .storage_directory = directory,
        .pin_display_callback = record_pin,
        .pin_dismiss_callback = dismiss_pin,
        .pin_user_data = &pin,
        .random_callback = deterministic_pairing_random};
    AirPlayPairingService *service = NULL;
    AirPlayRtspSession session;
    AirPlayRtspResponse response = {0};

    CHECK(airplay_pairing_service_create(&config, &service));
    airplay_rtsp_session_init(&session, 3);
    CHECK(dispatch(service, &session, "SETUP", "/stream", NULL, 0, NULL, &response));
    CHECK(response.status_code == 470);
    airplay_rtsp_response_clear(&response);
    airplay_pairing_session_closed(&session, service);

    airplay_rtsp_session_init(&session, 4);
    CHECK(start_and_challenge(service, &session, &pin, &response));
    CHECK(send_srp_proof(service, &session, false, &response));
    CHECK(response.close_connection);
    airplay_rtsp_response_clear(&response);
    airplay_pairing_session_closed(&session, service);

    airplay_rtsp_session_init(&session, 5);
    CHECK(start_and_challenge(service, &session, &pin, &response));
    CHECK(send_srp_proof(service, &session, true, &response));
    {
        uint8_t malformed[] = {0x62, 0x70, 0x6c, 0x69, 0x73, 0x74};
        CHECK(dispatch(service, &session, "POST", "/pair-setup-pin", malformed,
                       sizeof(malformed), "application/x-apple-binary-plist", &response));
        CHECK(response.status_code == 470);
        airplay_rtsp_response_clear(&response);
    }
    airplay_pairing_session_closed(&session, service);

    airplay_rtsp_session_init(&session, 6);
    CHECK(start_and_challenge(service, &session, &pin, &response));
    airplay_pairing_session_closed(&session, service);

    airplay_rtsp_session_init(&session, 7);
    CHECK(start_and_challenge(service, &session, &pin, &response));
    CHECK(send_srp_proof(service, &session, true, &response));
    CHECK(send_srp_proof(service, &session, false, &response));
    CHECK(response.status_code == 470 && response.close_connection);
    airplay_rtsp_response_clear(&response);
    airplay_pairing_session_closed(&session, service);
    airplay_pairing_service_destroy(service);
}

int main(void)
{
    char directory[] = "/tmp/nxcast-airplay-pairing-XXXXXX";
    char path[512];

    CHECK(mkdtemp(directory) != NULL);
    test_pairing_flow_and_persistence(directory);
    test_failures(directory);

    snprintf(path, sizeof(path), "%s/identity.bin", directory);
    remove(path);
    snprintf(path, sizeof(path), "%s/pairings.bin", directory);
    remove(path);
    snprintf(path, sizeof(path), "%s/pairings.bin.corrupt", directory);
    remove(path);
    CHECK(rmdir(directory) == 0);

    if (g_failures != 0)
    {
        fprintf(stderr, "AirPlay pairing tests failed: %d\n", g_failures);
        return 1;
    }
    puts("AirPlay pairing tests passed");
    return 0;
}
