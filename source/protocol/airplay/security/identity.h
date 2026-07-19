#ifndef NXCAST_AIRPLAY_SECURITY_IDENTITY_H
#define NXCAST_AIRPLAY_SECURITY_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "crypto.h"

#define AIRPLAY_IDENTITY_DEVICE_ID_SIZE 6u
#define AIRPLAY_IDENTITY_FINGERPRINT_SIZE AIRPLAY_CRYPTO_SHA256_SIZE
#define AIRPLAY_IDENTITY_DEFAULT_DIR "sdmc:/switch/NX-Cast/airplay"

typedef struct AirPlayIdentity AirPlayIdentity;

typedef enum
{
    AIRPLAY_IDENTITY_LOADED = 0,
    AIRPLAY_IDENTITY_CREATED,
    AIRPLAY_IDENTITY_RECOVERED
} AirPlayIdentityLoadResult;

bool airplay_identity_load_or_create(const char *directory,
                                     AirPlayCryptoRng *rng,
                                     AirPlayIdentity **identity,
                                     AirPlayIdentityLoadResult *load_result);
void airplay_identity_destroy(AirPlayIdentity *identity);

bool airplay_identity_device_id(const AirPlayIdentity *identity,
                                uint8_t output[AIRPLAY_IDENTITY_DEVICE_ID_SIZE]);
bool airplay_identity_device_id_string(const AirPlayIdentity *identity,
                                       char *output, size_t output_size);
bool airplay_identity_fingerprint(const AirPlayIdentity *identity,
                                  uint8_t output[AIRPLAY_IDENTITY_FINGERPRINT_SIZE]);
bool airplay_identity_public_key(
    const AirPlayIdentity *identity,
    uint8_t output[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE]);
bool airplay_identity_sign(
    const AirPlayIdentity *identity,
    const uint8_t *message, size_t message_size,
    uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE]);

#endif
