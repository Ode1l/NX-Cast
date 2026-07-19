#include "crypto.h"

#include <stdlib.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/sha1.h>

#if defined(AIRPLAY_CRYPTO_HAVE_ED25519)
#include <sodium.h>
#endif

typedef struct
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
} AirPlayCryptoRngState;

typedef struct
{
    mbedtls_aes_context aes;
} AirPlayCryptoAesCtrState;

static bool buffer_args_valid(const void *buffer, size_t size)
{
    return size == 0 || buffer != NULL;
}

void airplay_crypto_secure_zero(void *data, size_t size)
{
    if (data && size > 0)
        mbedtls_platform_zeroize(data, size);
}

bool airplay_crypto_equal(const void *left, const void *right, size_t size)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    uint8_t difference = 0;

    if (!buffer_args_valid(left, size) || !buffer_args_valid(right, size))
        return false;
    for (size_t i = 0; i < size; ++i)
        difference |= (uint8_t)(left_bytes[i] ^ right_bytes[i]);
    return difference == 0;
}

bool airplay_crypto_rng_init(AirPlayCryptoRng *rng, const char *personalization)
{
    AirPlayCryptoRngState *state;
    const uint8_t *label = (const uint8_t *)(personalization ? personalization : "NX-Cast AirPlay");
    size_t label_size = strlen((const char *)label);

    if (!rng || rng->state)
        return false;
    state = calloc(1, sizeof(*state));
    if (!state)
        return false;

    mbedtls_entropy_init(&state->entropy);
    mbedtls_ctr_drbg_init(&state->drbg);
    if (mbedtls_ctr_drbg_seed(&state->drbg, mbedtls_entropy_func, &state->entropy,
                              label, label_size) != 0)
    {
        mbedtls_ctr_drbg_free(&state->drbg);
        mbedtls_entropy_free(&state->entropy);
        airplay_crypto_secure_zero(state, sizeof(*state));
        free(state);
        return false;
    }
    rng->state = state;
    return true;
}

void airplay_crypto_rng_deinit(AirPlayCryptoRng *rng)
{
    AirPlayCryptoRngState *state;

    if (!rng || !rng->state)
        return;
    state = rng->state;
    rng->state = NULL;
    mbedtls_ctr_drbg_free(&state->drbg);
    mbedtls_entropy_free(&state->entropy);
    airplay_crypto_secure_zero(state, sizeof(*state));
    free(state);
}

bool airplay_crypto_random(AirPlayCryptoRng *rng, uint8_t *output, size_t output_size)
{
    AirPlayCryptoRngState *state;
    size_t offset = 0;

    if (!rng || !rng->state || !buffer_args_valid(output, output_size))
        return false;
    state = rng->state;
    while (offset < output_size)
    {
        size_t chunk = output_size - offset;
        if (chunk > MBEDTLS_CTR_DRBG_MAX_REQUEST)
            chunk = MBEDTLS_CTR_DRBG_MAX_REQUEST;
        if (mbedtls_ctr_drbg_random(&state->drbg, output + offset, chunk) != 0)
        {
            airplay_crypto_secure_zero(output, output_size);
            return false;
        }
        offset += chunk;
    }
    return true;
}

bool airplay_crypto_sha256(const void *input, size_t input_size,
                           uint8_t output[AIRPLAY_CRYPTO_SHA256_SIZE])
{
    if (!buffer_args_valid(input, input_size) || !output)
        return false;
    return mbedtls_sha256_ret(input, input_size, output, 0) == 0;
}

bool airplay_crypto_sha512(const void *input, size_t input_size,
                           uint8_t output[AIRPLAY_CRYPTO_SHA512_SIZE])
{
    if (!buffer_args_valid(input, input_size) || !output)
        return false;
    return mbedtls_sha512_ret(input, input_size, output, 0) == 0;
}

bool airplay_crypto_sha1(const void *input, size_t input_size,
                         uint8_t output[AIRPLAY_CRYPTO_SHA1_SIZE])
{
    if (!buffer_args_valid(input, input_size) || !output)
        return false;
    return mbedtls_sha1_ret(input, input_size, output) == 0;
}

bool airplay_crypto_hmac_sha256(const uint8_t *key, size_t key_size,
                                const void *input, size_t input_size,
                                uint8_t output[AIRPLAY_CRYPTO_SHA256_SIZE])
{
    const mbedtls_md_info_t *info;

    if (!buffer_args_valid(key, key_size) || !buffer_args_valid(input, input_size) || !output)
        return false;
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, key_size, input, input_size, output) == 0;
}

bool airplay_crypto_hkdf_sha256(const uint8_t *salt, size_t salt_size,
                                const uint8_t *input_key, size_t input_key_size,
                                const uint8_t *info_data, size_t info_size,
                                uint8_t *output, size_t output_size)
{
    const mbedtls_md_info_t *info;

    if (!buffer_args_valid(salt, salt_size) ||
        !buffer_args_valid(input_key, input_key_size) ||
        !buffer_args_valid(info_data, info_size) ||
        !buffer_args_valid(output, output_size))
        return false;
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_hkdf(info, salt, salt_size, input_key, input_key_size,
                                info_data, info_size, output, output_size) == 0;
}

static bool x25519_prepare_private(const uint8_t input[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
                                   mbedtls_mpi *private_mpi)
{
    uint8_t scalar[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
    int result;

    memcpy(scalar, input, sizeof(scalar));
    scalar[0] &= 248u;
    scalar[31] &= 127u;
    scalar[31] |= 64u;
    result = mbedtls_mpi_read_binary_le(private_mpi, scalar, sizeof(scalar));
    airplay_crypto_secure_zero(scalar, sizeof(scalar));
    return result == 0;
}

bool airplay_crypto_x25519_public(
    AirPlayCryptoRng *rng,
    const uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE])
{
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi private_mpi;
    AirPlayCryptoRngState *rng_state;
    int result = -1;

    if (!rng || !rng->state || !private_key || !public_key)
        return false;
    rng_state = rng->state;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&private_mpi);
    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
        x25519_prepare_private(private_key, &private_mpi))
    {
        result = mbedtls_ecp_mul(&group, &point, &private_mpi, &group.G,
                                 mbedtls_ctr_drbg_random, &rng_state->drbg);
        if (result == 0)
            result = mbedtls_mpi_write_binary_le(&point.X, public_key, AIRPLAY_CRYPTO_X25519_KEY_SIZE);
    }
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    if (result != 0)
        airplay_crypto_secure_zero(public_key, AIRPLAY_CRYPTO_X25519_KEY_SIZE);
    return result == 0;
}

bool airplay_crypto_x25519_shared(
    AirPlayCryptoRng *rng,
    const uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    const uint8_t peer_public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t shared_secret[AIRPLAY_CRYPTO_X25519_KEY_SIZE])
{
    mbedtls_ecp_group group;
    mbedtls_ecp_point peer;
    mbedtls_mpi private_mpi;
    mbedtls_mpi shared_mpi;
    uint8_t peer_bytes[AIRPLAY_CRYPTO_X25519_KEY_SIZE];
    uint8_t zero[AIRPLAY_CRYPTO_X25519_KEY_SIZE] = {0};
    AirPlayCryptoRngState *rng_state;
    int result = -1;

    if (!rng || !rng->state || !private_key || !peer_public_key || !shared_secret)
        return false;
    rng_state = rng->state;
    memcpy(peer_bytes, peer_public_key, sizeof(peer_bytes));
    peer_bytes[31] &= 127u;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&peer);
    mbedtls_mpi_init(&private_mpi);
    mbedtls_mpi_init(&shared_mpi);
    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
        x25519_prepare_private(private_key, &private_mpi) &&
        mbedtls_mpi_read_binary_le(&peer.X, peer_bytes, sizeof(peer_bytes)) == 0 &&
        mbedtls_mpi_lset(&peer.Z, 1) == 0)
    {
        result = mbedtls_ecdh_compute_shared(&group, &shared_mpi, &peer, &private_mpi,
                                             mbedtls_ctr_drbg_random, &rng_state->drbg);
        if (result == 0)
            result = mbedtls_mpi_write_binary_le(&shared_mpi, shared_secret,
                                                 AIRPLAY_CRYPTO_X25519_KEY_SIZE);
        if (result == 0 && airplay_crypto_equal(shared_secret, zero, sizeof(zero)))
            result = -1;
    }
    airplay_crypto_secure_zero(peer_bytes, sizeof(peer_bytes));
    mbedtls_mpi_free(&shared_mpi);
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_point_free(&peer);
    mbedtls_ecp_group_free(&group);
    if (result != 0)
        airplay_crypto_secure_zero(shared_secret, AIRPLAY_CRYPTO_X25519_KEY_SIZE);
    return result == 0;
}

bool airplay_crypto_x25519_keypair(
    AirPlayCryptoRng *rng,
    uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE])
{
    if (!rng || !private_key || !public_key ||
        !airplay_crypto_random(rng, private_key, AIRPLAY_CRYPTO_X25519_KEY_SIZE) ||
        !airplay_crypto_x25519_public(rng, private_key, public_key))
    {
        if (private_key)
            airplay_crypto_secure_zero(private_key, AIRPLAY_CRYPTO_X25519_KEY_SIZE);
        if (public_key)
            airplay_crypto_secure_zero(public_key, AIRPLAY_CRYPTO_X25519_KEY_SIZE);
        return false;
    }
    return true;
}

bool airplay_crypto_ed25519_public(
    const uint8_t seed[AIRPLAY_CRYPTO_ED25519_SEED_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE])
{
#if defined(AIRPLAY_CRYPTO_HAVE_ED25519)
    uint8_t secret_key[crypto_sign_SECRETKEYBYTES];
    int result;

    if (!seed || !public_key || sodium_init() < 0)
        return false;
    result = crypto_sign_seed_keypair(public_key, secret_key, seed);
    airplay_crypto_secure_zero(secret_key, sizeof(secret_key));
    if (result != 0)
        airplay_crypto_secure_zero(public_key, AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE);
    return result == 0;
#else
    (void)seed;
    if (public_key)
        airplay_crypto_secure_zero(public_key, AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE);
    return false;
#endif
}

bool airplay_crypto_ed25519_available(void)
{
#if defined(AIRPLAY_CRYPTO_HAVE_ED25519)
    return sodium_init() >= 0;
#else
    return false;
#endif
}

bool airplay_crypto_ed25519_sign(
    const uint8_t seed[AIRPLAY_CRYPTO_ED25519_SEED_SIZE],
    const uint8_t *message, size_t message_size,
    uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE])
{
#if defined(AIRPLAY_CRYPTO_HAVE_ED25519)
    uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
    uint8_t secret_key[crypto_sign_SECRETKEYBYTES];
    unsigned long long signature_size = 0;
    int result;

    if (!seed || !buffer_args_valid(message, message_size) || !signature || sodium_init() < 0)
        return false;
    result = crypto_sign_seed_keypair(public_key, secret_key, seed);
    if (result == 0)
        result = crypto_sign_detached(signature, &signature_size, message,
                                      (unsigned long long)message_size, secret_key);
    airplay_crypto_secure_zero(public_key, sizeof(public_key));
    airplay_crypto_secure_zero(secret_key, sizeof(secret_key));
    if (result != 0 || signature_size != AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE)
    {
        airplay_crypto_secure_zero(signature, AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE);
        return false;
    }
    return true;
#else
    (void)seed;
    (void)message;
    (void)message_size;
    if (signature)
        airplay_crypto_secure_zero(signature, AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE);
    return false;
#endif
}

bool airplay_crypto_ed25519_verify(
    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE],
    const uint8_t *message, size_t message_size,
    const uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE])
{
#if defined(AIRPLAY_CRYPTO_HAVE_ED25519)
    if (!public_key || !buffer_args_valid(message, message_size) || !signature || sodium_init() < 0)
        return false;
    return crypto_sign_verify_detached(signature, message,
                                       (unsigned long long)message_size, public_key) == 0;
#else
    (void)public_key;
    (void)message;
    (void)message_size;
    (void)signature;
    return false;
#endif
}

bool airplay_crypto_aes_ctr_init(AirPlayCryptoAesCtr *context,
                                 const uint8_t *key, size_t key_size,
                                 const uint8_t counter[AIRPLAY_CRYPTO_AES_BLOCK_SIZE])
{
    AirPlayCryptoAesCtrState *state;

    if (!context || context->state || !key || !counter ||
        (key_size != 16 && key_size != 24 && key_size != 32))
        return false;
    state = calloc(1, sizeof(*state));
    if (!state)
        return false;
    mbedtls_aes_init(&state->aes);
    if (mbedtls_aes_setkey_enc(&state->aes, key, (unsigned int)(key_size * 8)) != 0)
    {
        mbedtls_aes_free(&state->aes);
        airplay_crypto_secure_zero(state, sizeof(*state));
        free(state);
        return false;
    }
    memcpy(context->counter, counter, AIRPLAY_CRYPTO_AES_BLOCK_SIZE);
    memset(context->stream_block, 0, AIRPLAY_CRYPTO_AES_BLOCK_SIZE);
    context->offset = 0;
    context->state = state;
    return true;
}

bool airplay_crypto_aes_ctr_crypt(AirPlayCryptoAesCtr *context,
                                  const uint8_t *input, uint8_t *output, size_t size)
{
    AirPlayCryptoAesCtrState *state;

    if (!context || !context->state || !buffer_args_valid(input, size) ||
        !buffer_args_valid(output, size))
        return false;
    state = context->state;
    return mbedtls_aes_crypt_ctr(&state->aes, size, &context->offset,
                                 context->counter, context->stream_block,
                                 input, output) == 0;
}

void airplay_crypto_aes_ctr_deinit(AirPlayCryptoAesCtr *context)
{
    AirPlayCryptoAesCtrState *state;

    if (!context)
        return;
    state = context->state;
    context->state = NULL;
    if (state)
    {
        mbedtls_aes_free(&state->aes);
        airplay_crypto_secure_zero(state, sizeof(*state));
        free(state);
    }
    airplay_crypto_secure_zero(context, sizeof(*context));
}

bool airplay_crypto_aes_cbc_decrypt(
    const uint8_t key[AIRPLAY_CRYPTO_AES_BLOCK_SIZE],
    const uint8_t iv[AIRPLAY_CRYPTO_AES_BLOCK_SIZE],
    const uint8_t *input, uint8_t *output, size_t size)
{
    mbedtls_aes_context context;
    uint8_t working_iv[AIRPLAY_CRYPTO_AES_BLOCK_SIZE];
    int result;

    if (!key || !iv || !buffer_args_valid(input, size) ||
        !buffer_args_valid(output, size) ||
        size % AIRPLAY_CRYPTO_AES_BLOCK_SIZE != 0u)
        return false;
    memcpy(working_iv, iv, sizeof(working_iv));
    mbedtls_aes_init(&context);
    result = mbedtls_aes_setkey_dec(&context, key, 128u);
    if (result == 0 && size != 0u)
        result = mbedtls_aes_crypt_cbc(&context, MBEDTLS_AES_DECRYPT, size,
                                       working_iv, input, output);
    mbedtls_aes_free(&context);
    airplay_crypto_secure_zero(working_iv, sizeof(working_iv));
    if (result != 0)
        airplay_crypto_secure_zero(output, size);
    return result == 0;
}

bool airplay_crypto_aes_gcm_encrypt(
    const uint8_t key[16], const uint8_t *nonce, size_t nonce_size,
    const uint8_t *aad, size_t aad_size,
    const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext, uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE])
{
    mbedtls_gcm_context context;
    int result;

    if (!key || !buffer_args_valid(nonce, nonce_size) || nonce_size == 0 ||
        !buffer_args_valid(aad, aad_size) || !buffer_args_valid(plaintext, plaintext_size) ||
        !buffer_args_valid(ciphertext, plaintext_size) || !tag)
        return false;
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (result == 0)
        result = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT,
                                          plaintext_size, nonce, nonce_size,
                                          aad, aad_size, plaintext, ciphertext,
                                          AIRPLAY_CRYPTO_AEAD_TAG_SIZE, tag);
    mbedtls_gcm_free(&context);
    if (result != 0)
    {
        airplay_crypto_secure_zero(ciphertext, plaintext_size);
        airplay_crypto_secure_zero(tag, AIRPLAY_CRYPTO_AEAD_TAG_SIZE);
    }
    return result == 0;
}

bool airplay_crypto_aes_gcm_decrypt(
    const uint8_t key[16], const uint8_t *nonce, size_t nonce_size,
    const uint8_t *aad, size_t aad_size,
    const uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE],
    const uint8_t *ciphertext, size_t ciphertext_size,
    uint8_t *plaintext)
{
    mbedtls_gcm_context context;
    int result;

    if (!key || !buffer_args_valid(nonce, nonce_size) || nonce_size == 0 ||
        !buffer_args_valid(aad, aad_size) || !tag ||
        !buffer_args_valid(ciphertext, ciphertext_size) ||
        !buffer_args_valid(plaintext, ciphertext_size))
        return false;
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (result == 0)
        result = mbedtls_gcm_auth_decrypt(&context, ciphertext_size, nonce, nonce_size,
                                          aad, aad_size, tag,
                                          AIRPLAY_CRYPTO_AEAD_TAG_SIZE,
                                          ciphertext, plaintext);
    mbedtls_gcm_free(&context);
    if (result != 0)
        airplay_crypto_secure_zero(plaintext, ciphertext_size);
    return result == 0;
}

bool airplay_crypto_chachapoly_encrypt(
    const uint8_t key[AIRPLAY_CRYPTO_CHACHA_KEY_SIZE],
    const uint8_t nonce[AIRPLAY_CRYPTO_CHACHA_NONCE_SIZE],
    const uint8_t *aad, size_t aad_size,
    const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext,
    uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE])
{
    mbedtls_chachapoly_context context;
    int result;

    if (!key || !nonce || !buffer_args_valid(aad, aad_size) ||
        !buffer_args_valid(plaintext, plaintext_size) ||
        !buffer_args_valid(ciphertext, plaintext_size) || !tag)
        return false;
    mbedtls_chachapoly_init(&context);
    result = mbedtls_chachapoly_setkey(&context, key);
    if (result == 0)
        result = mbedtls_chachapoly_encrypt_and_tag(&context, plaintext_size, nonce,
                                                    aad, aad_size, plaintext,
                                                    ciphertext, tag);
    mbedtls_chachapoly_free(&context);
    if (result != 0)
    {
        airplay_crypto_secure_zero(ciphertext, plaintext_size);
        airplay_crypto_secure_zero(tag, AIRPLAY_CRYPTO_AEAD_TAG_SIZE);
    }
    return result == 0;
}

bool airplay_crypto_chachapoly_decrypt(
    const uint8_t key[AIRPLAY_CRYPTO_CHACHA_KEY_SIZE],
    const uint8_t nonce[AIRPLAY_CRYPTO_CHACHA_NONCE_SIZE],
    const uint8_t *aad, size_t aad_size,
    const uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE],
    const uint8_t *ciphertext, size_t ciphertext_size,
    uint8_t *plaintext)
{
    mbedtls_chachapoly_context context;
    int result;

    if (!key || !nonce || !buffer_args_valid(aad, aad_size) || !tag ||
        !buffer_args_valid(ciphertext, ciphertext_size) ||
        !buffer_args_valid(plaintext, ciphertext_size))
        return false;
    mbedtls_chachapoly_init(&context);
    result = mbedtls_chachapoly_setkey(&context, key);
    if (result == 0)
        result = mbedtls_chachapoly_auth_decrypt(&context, ciphertext_size, nonce,
                                                 aad, aad_size, tag, ciphertext,
                                                 plaintext);
    mbedtls_chachapoly_free(&context);
    if (result != 0)
        airplay_crypto_secure_zero(plaintext, ciphertext_size);
    return result == 0;
}
