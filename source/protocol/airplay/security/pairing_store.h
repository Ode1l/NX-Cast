#ifndef NXCAST_AIRPLAY_SECURITY_PAIRING_STORE_H
#define NXCAST_AIRPLAY_SECURITY_PAIRING_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "crypto.h"
#include "srp.h"

#define AIRPLAY_PAIRING_STORE_MAX_CLIENTS 16u

typedef struct AirPlayPairingStore AirPlayPairingStore;

bool airplay_pairing_store_open(const char *directory, AirPlayPairingStore **store_out);
void airplay_pairing_store_close(AirPlayPairingStore *store);
bool airplay_pairing_store_contains(const AirPlayPairingStore *store,
                                    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE]);
bool airplay_pairing_store_upsert(
    AirPlayPairingStore *store,
    const char *username,
    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE]);

#endif
