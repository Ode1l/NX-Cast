#ifndef NXCAST_AIRPLAY_SECURITY_CRYPTO_H
#define NXCAST_AIRPLAY_SECURITY_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_CRYPTO_SHA256_SIZE 32u
#define AIRPLAY_CRYPTO_SHA512_SIZE 64u
#define AIRPLAY_CRYPTO_X25519_KEY_SIZE 32u
#define AIRPLAY_CRYPTO_CHACHA_KEY_SIZE 32u
#define AIRPLAY_CRYPTO_CHACHA_NONCE_SIZE 12u
#define AIRPLAY_CRYPTO_AEAD_TAG_SIZE 16u
#define AIRPLAY_CRYPTO_AES_BLOCK_SIZE 16u
#define AIRPLAY_CRYPTO_ED25519_SEED_SIZE 32u
#define AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE 32u
#define AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE 64u

typedef struct
{
    void *state;
} AirPlayCryptoRng;

typedef struct
{
    void *state;
    uint8_t counter[AIRPLAY_CRYPTO_AES_BLOCK_SIZE];
    uint8_t stream_block[AIRPLAY_CRYPTO_AES_BLOCK_SIZE];
    size_t offset;
} AirPlayCryptoAesCtr;

bool airplay_crypto_rng_init(AirPlayCryptoRng *rng, const char *personalization);
void airplay_crypto_rng_deinit(AirPlayCryptoRng *rng);
bool airplay_crypto_random(AirPlayCryptoRng *rng, uint8_t *output, size_t output_size);

bool airplay_crypto_sha256(const void *input, size_t input_size,
                           uint8_t output[AIRPLAY_CRYPTO_SHA256_SIZE]);
bool airplay_crypto_sha512(const void *input, size_t input_size,
                           uint8_t output[AIRPLAY_CRYPTO_SHA512_SIZE]);
bool airplay_crypto_hmac_sha256(const uint8_t *key, size_t key_size,
                                const void *input, size_t input_size,
                                uint8_t output[AIRPLAY_CRYPTO_SHA256_SIZE]);
bool airplay_crypto_hkdf_sha256(const uint8_t *salt, size_t salt_size,
                                const uint8_t *input_key, size_t input_key_size,
                                const uint8_t *info, size_t info_size,
                                uint8_t *output, size_t output_size);

bool airplay_crypto_x25519_public(
    AirPlayCryptoRng *rng,
    const uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE]);
bool airplay_crypto_x25519_shared(
    AirPlayCryptoRng *rng,
    const uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    const uint8_t peer_public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t shared_secret[AIRPLAY_CRYPTO_X25519_KEY_SIZE]);
bool airplay_crypto_x25519_keypair(
    AirPlayCryptoRng *rng,
    uint8_t private_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_X25519_KEY_SIZE]);

bool airplay_crypto_ed25519_public(
    const uint8_t seed[AIRPLAY_CRYPTO_ED25519_SEED_SIZE],
    uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE]);
bool airplay_crypto_ed25519_sign(
    const uint8_t seed[AIRPLAY_CRYPTO_ED25519_SEED_SIZE],
    const uint8_t *message, size_t message_size,
    uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE]);
bool airplay_crypto_ed25519_verify(
    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE],
    const uint8_t *message, size_t message_size,
    const uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE]);
bool airplay_crypto_ed25519_available(void);

bool airplay_crypto_aes_ctr_init(AirPlayCryptoAesCtr *context,
                                 const uint8_t *key, size_t key_size,
                                 const uint8_t counter[AIRPLAY_CRYPTO_AES_BLOCK_SIZE]);
bool airplay_crypto_aes_ctr_crypt(AirPlayCryptoAesCtr *context,
                                  const uint8_t *input, uint8_t *output, size_t size);
void airplay_crypto_aes_ctr_deinit(AirPlayCryptoAesCtr *context);

bool airplay_crypto_chachapoly_encrypt(
    const uint8_t key[AIRPLAY_CRYPTO_CHACHA_KEY_SIZE],
    const uint8_t nonce[AIRPLAY_CRYPTO_CHACHA_NONCE_SIZE],
    const uint8_t *aad, size_t aad_size,
    const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext,
    uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE]);
bool airplay_crypto_chachapoly_decrypt(
    const uint8_t key[AIRPLAY_CRYPTO_CHACHA_KEY_SIZE],
    const uint8_t nonce[AIRPLAY_CRYPTO_CHACHA_NONCE_SIZE],
    const uint8_t *aad, size_t aad_size,
    const uint8_t tag[AIRPLAY_CRYPTO_AEAD_TAG_SIZE],
    const uint8_t *ciphertext, size_t ciphertext_size,
    uint8_t *plaintext);

bool airplay_crypto_equal(const void *left, const void *right, size_t size);
void airplay_crypto_secure_zero(void *data, size_t size);

#endif
