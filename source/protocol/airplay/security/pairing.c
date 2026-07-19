#include "pairing.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "crypto.h"
#include "identity.h"
#include "pairing_store.h"
#include "protocol/airplay/protocol/plist.h"
#include "srp.h"

#define PAIRING_PLIST_CONTENT_TYPE "application/x-apple-binary-plist"
#define PAIRING_VERIFY_MESSAGE_SIZE 64u

typedef struct
{
    struct AirPlayPairingService *service;
    AirPlayPairingState state;
    AirPlayCryptoRng rng;
    AirPlaySrpServer *srp;
    char pin[5];
    char username[AIRPLAY_SRP_USERNAME_MAX + 1u];
    uint8_t client_ed_public[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE];
    uint8_t client_x_public[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
    uint8_t server_x_private[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
    uint8_t server_x_public[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
    uint8_t shared_secret[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
} AirPlayPairingSession;

struct AirPlayPairingService
{
    AirPlayIdentity *identity;
    AirPlayPairingStore *store;
    AirPlayPinDisplayCallback pin_display_callback;
    AirPlayPinDismissCallback pin_dismiss_callback;
    void *pin_user_data;
#if defined(AIRPLAY_TESTING)
    AirPlayPairingRandomCallback random_callback;
    void *random_context;
#endif
    atomic_flag store_lock;
};

static bool pairing_rng(void *context, uint8_t *output, size_t size)
{
    AirPlayPairingSession *pairing = context;

#if defined(AIRPLAY_TESTING)
    if (pairing->service->random_callback)
        return pairing->service->random_callback(pairing->service->random_context,
                                                 output, size);
#endif
    return airplay_crypto_random(&pairing->rng, output, size);
}

static void pairing_lock(AirPlayPairingService *service)
{
    while (atomic_flag_test_and_set_explicit(&service->store_lock, memory_order_acquire))
    {
    }
}

static void pairing_unlock(AirPlayPairingService *service)
{
    atomic_flag_clear_explicit(&service->store_lock, memory_order_release);
}

static void pairing_session_destroy(AirPlayPairingSession *pairing)
{
    if (!pairing)
        return;
    airplay_srp_server_destroy(pairing->srp);
    pairing->srp = NULL;
    airplay_crypto_rng_deinit(&pairing->rng);
    airplay_crypto_secure_zero(pairing, sizeof(*pairing));
    free(pairing);
}

static AirPlayPairingSession *pairing_session_get(AirPlayRtspSession *session,
                                                  AirPlayPairingService *service)
{
    AirPlayPairingSession *pairing;

    if (!session)
        return NULL;
    pairing = session->security_context;
    if (pairing)
        return pairing;
    pairing = calloc(1, sizeof(*pairing));
    if (!pairing || !airplay_crypto_rng_init(&pairing->rng, "NX-Cast AirPlay pairing session"))
    {
        pairing_session_destroy(pairing);
        return NULL;
    }
    pairing->state = AIRPLAY_PAIRING_STATE_IDLE;
    pairing->service = service;
    session->security_context = pairing;
    return pairing;
}

bool airplay_pairing_service_create(const AirPlayPairingConfig *config,
                                    AirPlayPairingService **service_out)
{
    AirPlayPairingService *service;
    AirPlayCryptoRng rng = {0};
    AirPlayIdentityLoadResult identity_result;

    if (!config || !config->storage_directory || !service_out || *service_out ||
        !airplay_crypto_ed25519_available())
        return false;
    service = calloc(1, sizeof(*service));
    if (!service)
        return false;
    atomic_flag_clear(&service->store_lock);
    service->pin_display_callback = config->pin_display_callback;
    service->pin_dismiss_callback = config->pin_dismiss_callback;
    service->pin_user_data = config->pin_user_data;
#if defined(AIRPLAY_TESTING)
    service->random_callback = config->random_callback;
    service->random_context = config->random_context;
#endif
    if (!airplay_crypto_rng_init(&rng, "NX-Cast AirPlay device identity") ||
        !airplay_identity_load_or_create(config->storage_directory, &rng,
                                         &service->identity, &identity_result) ||
        !airplay_pairing_store_open(config->storage_directory, &service->store))
    {
        airplay_crypto_rng_deinit(&rng);
        airplay_pairing_service_destroy(service);
        return false;
    }
    airplay_crypto_rng_deinit(&rng);
    *service_out = service;
    return true;
}

void airplay_pairing_service_destroy(AirPlayPairingService *service)
{
    if (!service)
        return;
    airplay_pairing_store_close(service->store);
    airplay_identity_destroy(service->identity);
    airplay_crypto_secure_zero(service, sizeof(*service));
    free(service);
}

static bool pairing_set_failure(AirPlayPairingService *service,
                                AirPlayPairingSession *pairing,
                                AirPlayRtspResponse *response)
{
    if (service->pin_dismiss_callback && pairing->pin[0])
        service->pin_dismiss_callback(service->pin_user_data);
    airplay_srp_server_destroy(pairing->srp);
    pairing->srp = NULL;
    airplay_crypto_secure_zero(pairing->pin, sizeof(pairing->pin));
    airplay_crypto_secure_zero(pairing->server_x_private, sizeof(pairing->server_x_private));
    airplay_crypto_secure_zero(pairing->shared_secret, sizeof(pairing->shared_secret));
    pairing->state = AIRPLAY_PAIRING_STATE_FAILED;
    airplay_rtsp_response_set_body(response, NULL, 0, NULL);
    response->close_connection = true;
    return airplay_rtsp_response_set_status(response, 470);
}

static bool pairing_make_pin(AirPlayPairingSession *pairing)
{
    uint8_t random_bytes[2];
    uint16_t value;

    do
    {
        if (!pairing_rng(pairing, random_bytes, sizeof(random_bytes)))
            return false;
        value = (uint16_t)(((uint16_t)random_bytes[0] << 8) | random_bytes[1]);
    } while (value >= 59994u);
    value = (uint16_t)(value % 9999u + 1u);
    snprintf(pairing->pin, sizeof(pairing->pin), "%04u", (unsigned)value);
    airplay_crypto_secure_zero(random_bytes, sizeof(random_bytes));
    return true;
}

static bool pairing_handle_pin_start(AirPlayPairingService *service,
                                     AirPlayPairingSession *pairing,
                                     const AirPlayRtspRequest *request,
                                     AirPlayRtspResponse *response)
{
    if (strcmp(request->method, "POST") != 0 || request->body_length != 0 ||
        pairing->state != AIRPLAY_PAIRING_STATE_IDLE || !pairing_make_pin(pairing))
        return pairing_set_failure(service, pairing, response);
    pairing->state = AIRPLAY_PAIRING_STATE_PIN_STARTED;
    if (service->pin_display_callback)
        service->pin_display_callback(pairing->pin, service->pin_user_data);
    return true;
}

static bool pairing_content_type_valid(const AirPlayRtspRequest *request)
{
    const char *content_type = airplay_rtsp_request_header(request, "Content-Type");
    size_t expected_size = strlen(PAIRING_PLIST_CONTENT_TYPE);

    return content_type && strncasecmp(content_type, PAIRING_PLIST_CONTENT_TYPE, expected_size) == 0 &&
           (content_type[expected_size] == '\0' || content_type[expected_size] == ';');
}

static bool pairing_set_plist_body(AirPlayRtspResponse *response, AirPlayPlistValue *root)
{
    uint8_t *encoded = NULL;
    size_t encoded_size = 0;
    AirPlayPlistError error;
    bool ok;

    ok = airplay_plist_encode(root, &encoded, &encoded_size, &error) &&
         airplay_rtsp_response_set_body(response, encoded, encoded_size,
                                        PAIRING_PLIST_CONTENT_TYPE);
    airplay_plist_buffer_free(encoded);
    return ok;
}

static bool pairing_plist_dict_data(AirPlayPlistValue *dict, const char *key,
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

static bool pairing_setup_challenge(AirPlayPairingService *service,
                                    AirPlayPairingSession *pairing,
                                    const AirPlayPlistValue *request,
                                    AirPlayRtspResponse *response)
{
    const char *method;
    const char *username;
    AirPlayPlistValue *root = NULL;
    bool ok = false;

    if (pairing->state != AIRPLAY_PAIRING_STATE_PIN_STARTED)
        return pairing_set_failure(service, pairing, response);
    method = airplay_plist_get_string(airplay_plist_dict_get(request, "method"));
    username = airplay_plist_get_string(airplay_plist_dict_get(request, "user"));
    if (!method || strcmp(method, "pin") != 0 || !username || strlen(username) == 0 ||
        strlen(username) > AIRPLAY_SRP_USERNAME_MAX ||
        !airplay_srp_server_create(username, pairing->pin, pairing_rng, pairing,
                                   &pairing->srp))
        return pairing_set_failure(service, pairing, response);
    memcpy(pairing->username, username, strlen(username) + 1u);
    root = airplay_plist_new_dict();
    if (root &&
        pairing_plist_dict_data(root, "pk", airplay_srp_server_public_key(pairing->srp),
                                AIRPLAY_SRP_PUBLIC_KEY_SIZE) &&
        pairing_plist_dict_data(root, "salt", airplay_srp_server_salt(pairing->srp),
                                AIRPLAY_SRP_SALT_SIZE) &&
        pairing_set_plist_body(response, root))
        ok = true;
    airplay_plist_free(root);
    if (!ok)
        return pairing_set_failure(service, pairing, response);
    if (service->pin_dismiss_callback)
        service->pin_dismiss_callback(service->pin_user_data);
    airplay_crypto_secure_zero(pairing->pin, sizeof(pairing->pin));
    pairing->state = AIRPLAY_PAIRING_STATE_SRP_CHALLENGE;
    return true;
}

static bool pairing_setup_proof(AirPlayPairingService *service,
                                AirPlayPairingSession *pairing,
                                const AirPlayPlistValue *request,
                                AirPlayRtspResponse *response)
{
    const uint8_t *client_public;
    const uint8_t *client_proof;
    size_t client_public_size;
    size_t client_proof_size;
    uint8_t server_proof[AIRPLAY_SRP_PROOF_SIZE];
    AirPlayPlistValue *root = NULL;
    bool ok = false;

    if (pairing->state != AIRPLAY_PAIRING_STATE_SRP_CHALLENGE || !pairing->srp)
        return pairing_set_failure(service, pairing, response);
    client_public = airplay_plist_get_data(airplay_plist_dict_get(request, "pk"),
                                           &client_public_size);
    client_proof = airplay_plist_get_data(airplay_plist_dict_get(request, "proof"),
                                          &client_proof_size);
    if (!client_public || client_public_size == 0 ||
        client_public_size > AIRPLAY_SRP_PUBLIC_KEY_SIZE || !client_proof ||
        client_proof_size != AIRPLAY_SRP_PROOF_SIZE ||
        !airplay_srp_server_verify(pairing->srp, client_public, client_public_size,
                                   client_proof, server_proof))
        return pairing_set_failure(service, pairing, response);
    root = airplay_plist_new_dict();
    if (root && pairing_plist_dict_data(root, "proof", server_proof,
                                        sizeof(server_proof)) &&
        pairing_set_plist_body(response, root))
        ok = true;
    airplay_plist_free(root);
    airplay_crypto_secure_zero(server_proof, sizeof(server_proof));
    if (!ok)
        return pairing_set_failure(service, pairing, response);
    pairing->state = AIRPLAY_PAIRING_STATE_SRP_VERIFIED;
    return true;
}

static bool pairing_derive_label(const char *label, const uint8_t *secret, size_t secret_size,
                                 uint8_t *output, size_t output_size)
{
    uint8_t input[96];
    uint8_t hash[AIRPLAY_CRYPTO_SHA512_SIZE];
    size_t label_size = strlen(label);
    bool ok;

    if (label_size + secret_size > sizeof(input) || output_size > sizeof(hash))
        return false;
    memcpy(input, label, label_size);
    memcpy(input + label_size, secret, secret_size);
    ok = airplay_crypto_sha512(input, label_size + secret_size, hash);
    if (ok)
        memcpy(output, hash, output_size);
    airplay_crypto_secure_zero(input, sizeof(input));
    airplay_crypto_secure_zero(hash, sizeof(hash));
    return ok;
}

static bool pairing_setup_exchange_key(AirPlayPairingService *service,
                                       AirPlayPairingSession *pairing,
                                       const AirPlayPlistValue *request,
                                       AirPlayRtspResponse *response)
{
    const uint8_t *encrypted_public;
    const uint8_t *client_tag;
    size_t encrypted_size;
    size_t tag_size;
    uint8_t session_key[AIRPLAY_SRP_SESSION_KEY_SIZE];
    uint8_t aes_key[16];
    uint8_t nonce[16];
    uint8_t server_public[32];
    uint8_t response_encrypted[32];
    uint8_t response_tag[16];
    AirPlayPlistValue *root = NULL;
    bool stored;
    bool ok = false;

    if (pairing->state != AIRPLAY_PAIRING_STATE_SRP_VERIFIED || !pairing->srp)
        return pairing_set_failure(service, pairing, response);
    encrypted_public = airplay_plist_get_data(airplay_plist_dict_get(request, "epk"),
                                              &encrypted_size);
    client_tag = airplay_plist_get_data(airplay_plist_dict_get(request, "authTag"),
                                        &tag_size);
    if (!encrypted_public || encrypted_size != sizeof(pairing->client_ed_public) ||
        !client_tag || tag_size != sizeof(response_tag) ||
        !airplay_srp_server_session_key(pairing->srp, session_key) ||
        !pairing_derive_label("Pair-Setup-AES-Key", session_key, sizeof(session_key),
                              aes_key, sizeof(aes_key)) ||
        !pairing_derive_label("Pair-Setup-AES-IV", session_key, sizeof(session_key),
                              nonce, sizeof(nonce)))
        goto cleanup;
    nonce[15]++;
    if (!airplay_crypto_aes_gcm_decrypt(aes_key, nonce, sizeof(nonce), NULL, 0,
                                        client_tag, encrypted_public, encrypted_size,
                                        pairing->client_ed_public) ||
        !airplay_identity_public_key(service->identity, server_public))
        goto cleanup;
    nonce[15]++;
    if (!airplay_crypto_aes_gcm_encrypt(aes_key, nonce, sizeof(nonce), NULL, 0,
                                        server_public, sizeof(server_public),
                                        response_encrypted, response_tag))
        goto cleanup;
    root = airplay_plist_new_dict();
    if (root && pairing_plist_dict_data(root, "epk", response_encrypted,
                                        sizeof(response_encrypted)) &&
        pairing_plist_dict_data(root, "authTag", response_tag, sizeof(response_tag)) &&
        pairing_set_plist_body(response, root))
        ok = true;
    if (ok)
    {
        pairing_lock(service);
        stored = airplay_pairing_store_upsert(service->store, pairing->username,
                                              pairing->client_ed_public);
        pairing_unlock(service);
        ok = stored;
    }

cleanup:
    airplay_plist_free(root);
    airplay_crypto_secure_zero(session_key, sizeof(session_key));
    airplay_crypto_secure_zero(aes_key, sizeof(aes_key));
    airplay_crypto_secure_zero(nonce, sizeof(nonce));
    airplay_crypto_secure_zero(server_public, sizeof(server_public));
    airplay_crypto_secure_zero(response_encrypted, sizeof(response_encrypted));
    airplay_crypto_secure_zero(response_tag, sizeof(response_tag));
    airplay_srp_server_destroy(pairing->srp);
    pairing->srp = NULL;
    if (!ok)
        return pairing_set_failure(service, pairing, response);
    pairing->state = AIRPLAY_PAIRING_STATE_PAIRED;
    return true;
}

static bool pairing_handle_setup(AirPlayPairingService *service,
                                 AirPlayPairingSession *pairing,
                                 const AirPlayRtspRequest *request,
                                 AirPlayRtspResponse *response)
{
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error;
    bool handled;

    if (strcmp(request->method, "POST") != 0 || !pairing_content_type_valid(request) ||
        !airplay_plist_decode(request->body, request->body_length, &root, &error) ||
        airplay_plist_type(root) != AIRPLAY_PLIST_TYPE_DICT)
    {
        airplay_plist_free(root);
        return pairing_set_failure(service, pairing, response);
    }
    if (airplay_plist_dict_get(root, "method") || airplay_plist_dict_get(root, "user"))
        handled = pairing_setup_challenge(service, pairing, root, response);
    else if (airplay_plist_dict_get(root, "proof"))
        handled = pairing_setup_proof(service, pairing, root, response);
    else if (airplay_plist_dict_get(root, "epk") || airplay_plist_dict_get(root, "authTag"))
        handled = pairing_setup_exchange_key(service, pairing, root, response);
    else
        handled = pairing_set_failure(service, pairing, response);
    airplay_plist_free(root);
    return handled;
}

static bool pairing_verify_start(AirPlayPairingService *service,
                                 AirPlayPairingSession *pairing,
                                 const uint8_t *body, size_t body_size,
                                 AirPlayRtspResponse *response)
{
    uint8_t signature[64];
    uint8_t message[64];
    uint8_t key[16];
    uint8_t nonce[16];
    uint8_t encrypted_signature[64];
    uint8_t response_body[96];
    bool registered;
    bool ok = false;

    if ((pairing->state != AIRPLAY_PAIRING_STATE_IDLE &&
         pairing->state != AIRPLAY_PAIRING_STATE_PAIRED) || body_size != 68)
        return pairing_set_failure(service, pairing, response);
    memcpy(pairing->client_x_public, body + 4, sizeof(pairing->client_x_public));
    memcpy(pairing->client_ed_public, body + 36, sizeof(pairing->client_ed_public));
    pairing_lock(service);
    registered = airplay_pairing_store_contains(service->store, pairing->client_ed_public);
    pairing_unlock(service);
    if (!registered ||
        !airplay_crypto_x25519_keypair(&pairing->rng, pairing->server_x_private,
                                       pairing->server_x_public) ||
        !airplay_crypto_x25519_shared(&pairing->rng, pairing->server_x_private,
                                      pairing->client_x_public, pairing->shared_secret))
        goto cleanup;
    memcpy(message, pairing->server_x_public, 32);
    memcpy(message + 32, pairing->client_x_public, 32);
    if (!airplay_identity_sign(service->identity, message, sizeof(message), signature) ||
        !pairing_derive_label("Pair-Verify-AES-Key", pairing->shared_secret,
                              sizeof(pairing->shared_secret), key, sizeof(key)) ||
        !pairing_derive_label("Pair-Verify-AES-IV", pairing->shared_secret,
                              sizeof(pairing->shared_secret), nonce, sizeof(nonce)))
        goto cleanup;
    {
        AirPlayCryptoAesCtr aes = {0};
        if (!airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), nonce) ||
            !airplay_crypto_aes_ctr_crypt(&aes, signature, encrypted_signature,
                                          sizeof(encrypted_signature)))
        {
            airplay_crypto_aes_ctr_deinit(&aes);
            goto cleanup;
        }
        airplay_crypto_aes_ctr_deinit(&aes);
    }
    memcpy(response_body, pairing->server_x_public, 32);
    memcpy(response_body + 32, encrypted_signature, 64);
    if (!airplay_rtsp_response_set_body(response, response_body, sizeof(response_body),
                                        "application/octet-stream"))
        goto cleanup;
    pairing->state = AIRPLAY_PAIRING_STATE_VERIFY_CHALLENGE;
    ok = true;

cleanup:
    airplay_crypto_secure_zero(signature, sizeof(signature));
    airplay_crypto_secure_zero(message, sizeof(message));
    airplay_crypto_secure_zero(key, sizeof(key));
    airplay_crypto_secure_zero(nonce, sizeof(nonce));
    airplay_crypto_secure_zero(encrypted_signature, sizeof(encrypted_signature));
    airplay_crypto_secure_zero(response_body, sizeof(response_body));
    if (!ok)
        return pairing_set_failure(service, pairing, response);
    return true;
}

static bool pairing_verify_finish(AirPlayPairingService *service,
                                  AirPlayPairingSession *pairing,
                                  const uint8_t *body, size_t body_size,
                                  AirPlayRtspResponse *response)
{
    uint8_t key[16];
    uint8_t nonce[16];
    uint8_t discard_input[64] = {0};
    uint8_t discard_output[64];
    uint8_t signature[64];
    uint8_t message[64];
    AirPlayCryptoAesCtr aes = {0};
    bool ok = false;

    if (pairing->state != AIRPLAY_PAIRING_STATE_VERIFY_CHALLENGE || body_size != 68 ||
        !pairing_derive_label("Pair-Verify-AES-Key", pairing->shared_secret,
                              sizeof(pairing->shared_secret), key, sizeof(key)) ||
        !pairing_derive_label("Pair-Verify-AES-IV", pairing->shared_secret,
                              sizeof(pairing->shared_secret), nonce, sizeof(nonce)) ||
        !airplay_crypto_aes_ctr_init(&aes, key, sizeof(key), nonce) ||
        !airplay_crypto_aes_ctr_crypt(&aes, discard_input, discard_output,
                                      sizeof(discard_output)) ||
        !airplay_crypto_aes_ctr_crypt(&aes, body + 4, signature, sizeof(signature)))
        goto cleanup;
    memcpy(message, pairing->client_x_public, 32);
    memcpy(message + 32, pairing->server_x_public, 32);
    if (!airplay_crypto_ed25519_verify(pairing->client_ed_public, message,
                                       sizeof(message), signature))
        goto cleanup;
    pairing->state = AIRPLAY_PAIRING_STATE_VERIFIED;
    airplay_crypto_secure_zero(pairing->server_x_private,
                               sizeof(pairing->server_x_private));
    ok = true;

cleanup:
    airplay_crypto_aes_ctr_deinit(&aes);
    airplay_crypto_secure_zero(key, sizeof(key));
    airplay_crypto_secure_zero(nonce, sizeof(nonce));
    airplay_crypto_secure_zero(discard_output, sizeof(discard_output));
    airplay_crypto_secure_zero(signature, sizeof(signature));
    airplay_crypto_secure_zero(message, sizeof(message));
    if (!ok)
        return pairing_set_failure(service, pairing, response);
    return true;
}

static bool pairing_handle_verify(AirPlayPairingService *service,
                                  AirPlayPairingSession *pairing,
                                  const AirPlayRtspRequest *request,
                                  AirPlayRtspResponse *response)
{
    if (strcmp(request->method, "POST") != 0 || request->body_length < 4)
        return pairing_set_failure(service, pairing, response);
    if (request->body[0] == 1)
        return pairing_verify_start(service, pairing, request->body,
                                    request->body_length, response);
    if (request->body[0] == 0)
        return pairing_verify_finish(service, pairing, request->body,
                                     request->body_length, response);
    return pairing_set_failure(service, pairing, response);
}

bool airplay_pairing_route(AirPlayRtspSession *session,
                           const AirPlayRtspRequest *request,
                           AirPlayRtspResponse *response,
                           void *user_data)
{
    AirPlayPairingService *service = user_data;
    AirPlayPairingSession *pairing;

    if (!service || !session || !request || !response)
        return false;
    pairing = pairing_session_get(session, service);
    if (!pairing)
        return false;
    if (strcmp(request->uri, "/pair-pin-start") == 0)
        return pairing_handle_pin_start(service, pairing, request, response);
    if (strcmp(request->uri, "/pair-setup-pin") == 0)
        return pairing_handle_setup(service, pairing, request, response);
    if (strcmp(request->uri, "/pair-verify") == 0)
        return pairing_handle_verify(service, pairing, request, response);
    if ((strcmp(request->method, "SETUP") == 0 || strcmp(request->method, "RECORD") == 0) &&
        pairing->state != AIRPLAY_PAIRING_STATE_VERIFIED)
        return pairing_set_failure(service, pairing, response);
    return airplay_rtsp_default_route(session, request, response);
}

void airplay_pairing_session_closed(AirPlayRtspSession *session, void *user_data)
{
    AirPlayPairingSession *pairing;
    (void)user_data;

    if (!session)
        return;
    pairing = session->security_context;
    session->security_context = NULL;
    pairing_session_destroy(pairing);
}

AirPlayPairingState airplay_pairing_session_state(const AirPlayRtspSession *session)
{
    const AirPlayPairingSession *pairing = session ? session->security_context : NULL;
    return pairing ? pairing->state : AIRPLAY_PAIRING_STATE_IDLE;
}

bool airplay_pairing_session_verified(const AirPlayRtspSession *session)
{
    return airplay_pairing_session_state(session) == AIRPLAY_PAIRING_STATE_VERIFIED;
}

bool airplay_pairing_session_shared_secret(const AirPlayRtspSession *session,
                                           uint8_t output[32])
{
    const AirPlayPairingSession *pairing = session ? session->security_context : NULL;

    if (!pairing || pairing->state != AIRPLAY_PAIRING_STATE_VERIFIED || !output)
        return false;
    memcpy(output, pairing->shared_secret, sizeof(pairing->shared_secret));
    return true;
}

const char *airplay_pairing_state_name(AirPlayPairingState state)
{
    switch (state)
    {
    case AIRPLAY_PAIRING_STATE_IDLE:
        return "idle";
    case AIRPLAY_PAIRING_STATE_PIN_STARTED:
        return "pin-started";
    case AIRPLAY_PAIRING_STATE_SRP_CHALLENGE:
        return "srp-challenge";
    case AIRPLAY_PAIRING_STATE_SRP_VERIFIED:
        return "srp-verified";
    case AIRPLAY_PAIRING_STATE_PAIRED:
        return "paired";
    case AIRPLAY_PAIRING_STATE_VERIFY_CHALLENGE:
        return "verify-challenge";
    case AIRPLAY_PAIRING_STATE_VERIFIED:
        return "verified";
    case AIRPLAY_PAIRING_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}
