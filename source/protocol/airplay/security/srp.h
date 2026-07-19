#ifndef NXCAST_AIRPLAY_SECURITY_SRP_H
#define NXCAST_AIRPLAY_SECURITY_SRP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_SRP_SALT_SIZE 16u
#define AIRPLAY_SRP_PUBLIC_KEY_SIZE 256u
#define AIRPLAY_SRP_PROOF_SIZE 20u
#define AIRPLAY_SRP_SESSION_KEY_SIZE 40u
#define AIRPLAY_SRP_USERNAME_MAX 127u

typedef bool (*AirPlaySrpRandomCallback)(void *context, uint8_t *output, size_t size);

typedef struct AirPlaySrpServer AirPlaySrpServer;

bool airplay_srp_server_create(const char *username,
                               const char pin[5],
                               AirPlaySrpRandomCallback random_callback,
                               void *random_context,
                               AirPlaySrpServer **server_out);
void airplay_srp_server_destroy(AirPlaySrpServer *server);

const uint8_t *airplay_srp_server_salt(const AirPlaySrpServer *server);
const uint8_t *airplay_srp_server_public_key(const AirPlaySrpServer *server);
bool airplay_srp_server_verify(AirPlaySrpServer *server,
                               const uint8_t *client_public_key,
                               size_t client_public_key_size,
                               const uint8_t client_proof[AIRPLAY_SRP_PROOF_SIZE],
                               uint8_t server_proof[AIRPLAY_SRP_PROOF_SIZE]);
bool airplay_srp_server_session_key(
    const AirPlaySrpServer *server,
    uint8_t output[AIRPLAY_SRP_SESSION_KEY_SIZE]);

#endif
