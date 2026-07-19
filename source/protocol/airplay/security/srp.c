#include "srp.h"

#include <stdlib.h>
#include <string.h>

#include <mbedtls/bignum.h>
#include <mbedtls/sha1.h>

#include "crypto.h"

#define AIRPLAY_SRP_PRIVATE_SIZE 32u

static const char g_srp_modulus_hex[] =
    "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4"
    "A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF60"
    "95179A163AB3661A05FBD5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF"
    "747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481F1D2B907"
    "8717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB37861"
    "60279004E57AE6AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DB"
    "FBB694B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73";

struct AirPlaySrpServer
{
    char username[AIRPLAY_SRP_USERNAME_MAX + 1u];
    uint8_t salt[AIRPLAY_SRP_SALT_SIZE];
    uint8_t public_key[AIRPLAY_SRP_PUBLIC_KEY_SIZE];
    uint8_t session_key[AIRPLAY_SRP_SESSION_KEY_SIZE];
    mbedtls_mpi modulus;
    mbedtls_mpi generator;
    mbedtls_mpi verifier;
    mbedtls_mpi private_key;
    mbedtls_mpi server_public;
    bool authenticated;
};

static void sha1_start(mbedtls_sha1_context *context)
{
    mbedtls_sha1_init(context);
    mbedtls_sha1_starts_ret(context);
}

static bool sha1_update(mbedtls_sha1_context *context, const void *data, size_t size)
{
    return size == 0 || (data && mbedtls_sha1_update_ret(context, data, size) == 0);
}

static bool sha1_finish(mbedtls_sha1_context *context,
                        uint8_t output[AIRPLAY_SRP_PROOF_SIZE])
{
    bool ok = mbedtls_sha1_finish_ret(context, output) == 0;
    mbedtls_sha1_free(context);
    return ok;
}

static bool mpi_write_minimal(const mbedtls_mpi *value,
                              uint8_t output[AIRPLAY_SRP_PUBLIC_KEY_SIZE],
                              size_t *size_out)
{
    size_t size;

    if (!value || !output || !size_out)
        return false;
    size = mbedtls_mpi_size(value);
    if (size == 0)
        size = 1;
    if (size > AIRPLAY_SRP_PUBLIC_KEY_SIZE)
        return false;
    memset(output, 0, AIRPLAY_SRP_PUBLIC_KEY_SIZE);
    if (mbedtls_mpi_write_binary(value, output, size) != 0)
        return false;
    *size_out = size;
    return true;
}

static bool mpi_write_padded(const mbedtls_mpi *value,
                             uint8_t output[AIRPLAY_SRP_PUBLIC_KEY_SIZE])
{
    if (!value || !output || mbedtls_mpi_size(value) > AIRPLAY_SRP_PUBLIC_KEY_SIZE)
        return false;
    return mbedtls_mpi_write_binary(value, output, AIRPLAY_SRP_PUBLIC_KEY_SIZE) == 0;
}

static bool hash_mpi_minimal(const mbedtls_mpi *value,
                             uint8_t output[AIRPLAY_SRP_PROOF_SIZE])
{
    uint8_t bytes[AIRPLAY_SRP_PUBLIC_KEY_SIZE];
    size_t size;
    bool ok;

    if (!mpi_write_minimal(value, bytes, &size))
        return false;
    ok = airplay_crypto_sha1(bytes, size, output);
    airplay_crypto_secure_zero(bytes, sizeof(bytes));
    return ok;
}

static bool hash_padded_pair(const mbedtls_mpi *left,
                             const mbedtls_mpi *right,
                             mbedtls_mpi *hash_value)
{
    uint8_t bytes[AIRPLAY_SRP_PUBLIC_KEY_SIZE * 2u];
    uint8_t digest[AIRPLAY_SRP_PROOF_SIZE];
    bool ok = false;

    if (mpi_write_padded(left, bytes) &&
        mpi_write_padded(right, bytes + AIRPLAY_SRP_PUBLIC_KEY_SIZE) &&
        airplay_crypto_sha1(bytes, sizeof(bytes), digest) &&
        mbedtls_mpi_read_binary(hash_value, digest, sizeof(digest)) == 0)
        ok = true;
    airplay_crypto_secure_zero(bytes, sizeof(bytes));
    airplay_crypto_secure_zero(digest, sizeof(digest));
    return ok;
}

static bool calculate_verifier(AirPlaySrpServer *server, const char pin[5])
{
    uint8_t credentials_hash[AIRPLAY_SRP_PROOF_SIZE];
    uint8_t x_hash[AIRPLAY_SRP_PROOF_SIZE];
    mbedtls_sha1_context hash;
    mbedtls_mpi x;
    bool ok = false;

    mbedtls_mpi_init(&x);
    sha1_start(&hash);
    if (!sha1_update(&hash, server->username, strlen(server->username)) ||
        !sha1_update(&hash, ":", 1) || !sha1_update(&hash, pin, 4) ||
        !sha1_finish(&hash, credentials_hash))
        goto cleanup;
    sha1_start(&hash);
    if (!sha1_update(&hash, server->salt, sizeof(server->salt)) ||
        !sha1_update(&hash, credentials_hash, sizeof(credentials_hash)) ||
        !sha1_finish(&hash, x_hash) ||
        mbedtls_mpi_read_binary(&x, x_hash, sizeof(x_hash)) != 0 ||
        mbedtls_mpi_exp_mod(&server->verifier, &server->generator, &x,
                            &server->modulus, NULL) != 0)
        goto cleanup;
    ok = true;

cleanup:
    mbedtls_mpi_free(&x);
    airplay_crypto_secure_zero(credentials_hash, sizeof(credentials_hash));
    airplay_crypto_secure_zero(x_hash, sizeof(x_hash));
    return ok;
}

static bool calculate_server_public(AirPlaySrpServer *server)
{
    mbedtls_mpi multiplier;
    mbedtls_mpi left;
    mbedtls_mpi right;
    bool ok = false;

    mbedtls_mpi_init(&multiplier);
    mbedtls_mpi_init(&left);
    mbedtls_mpi_init(&right);
    if (!hash_padded_pair(&server->modulus, &server->generator, &multiplier) ||
        mbedtls_mpi_mul_mpi(&left, &multiplier, &server->verifier) != 0 ||
        mbedtls_mpi_mod_mpi(&left, &left, &server->modulus) != 0 ||
        mbedtls_mpi_exp_mod(&right, &server->generator, &server->private_key,
                            &server->modulus, NULL) != 0 ||
        mbedtls_mpi_add_mpi(&server->server_public, &left, &right) != 0 ||
        mbedtls_mpi_mod_mpi(&server->server_public, &server->server_public,
                            &server->modulus) != 0 ||
        !mpi_write_padded(&server->server_public, server->public_key))
        goto cleanup;
    ok = true;

cleanup:
    mbedtls_mpi_free(&right);
    mbedtls_mpi_free(&left);
    mbedtls_mpi_free(&multiplier);
    return ok;
}

bool airplay_srp_server_create(const char *username,
                               const char pin[5],
                               AirPlaySrpRandomCallback random_callback,
                               void *random_context,
                               AirPlaySrpServer **server_out)
{
    AirPlaySrpServer *server;
    uint8_t private_bytes[AIRPLAY_SRP_PRIVATE_SIZE];
    size_t username_size;
    bool ok = false;

    if (!username || !pin || !random_callback || !server_out || *server_out)
        return false;
    username_size = strlen(username);
    if (username_size == 0 || username_size > AIRPLAY_SRP_USERNAME_MAX ||
        strlen(pin) != 4)
        return false;
    for (size_t i = 0; i < 4; ++i)
    {
        if (pin[i] < '0' || pin[i] > '9')
            return false;
    }
    server = calloc(1, sizeof(*server));
    if (!server)
        return false;
    mbedtls_mpi_init(&server->modulus);
    mbedtls_mpi_init(&server->generator);
    mbedtls_mpi_init(&server->verifier);
    mbedtls_mpi_init(&server->private_key);
    mbedtls_mpi_init(&server->server_public);
    memcpy(server->username, username, username_size + 1u);

    if (!random_callback(random_context, server->salt, sizeof(server->salt)) ||
        !random_callback(random_context, private_bytes, sizeof(private_bytes)))
        goto cleanup;
    server->salt[0] |= 0x80u;
    private_bytes[0] |= 0x80u;
    if (mbedtls_mpi_read_string(&server->modulus, 16, g_srp_modulus_hex) != 0 ||
        mbedtls_mpi_lset(&server->generator, 2) != 0 ||
        mbedtls_mpi_read_binary(&server->private_key, private_bytes,
                                sizeof(private_bytes)) != 0 ||
        !calculate_verifier(server, pin) || !calculate_server_public(server))
        goto cleanup;
    *server_out = server;
    ok = true;

cleanup:
    airplay_crypto_secure_zero(private_bytes, sizeof(private_bytes));
    if (!ok)
        airplay_srp_server_destroy(server);
    return ok;
}

void airplay_srp_server_destroy(AirPlaySrpServer *server)
{
    if (!server)
        return;
    mbedtls_mpi_free(&server->server_public);
    mbedtls_mpi_free(&server->private_key);
    mbedtls_mpi_free(&server->verifier);
    mbedtls_mpi_free(&server->generator);
    mbedtls_mpi_free(&server->modulus);
    airplay_crypto_secure_zero(server, sizeof(*server));
    free(server);
}

const uint8_t *airplay_srp_server_salt(const AirPlaySrpServer *server)
{
    return server ? server->salt : NULL;
}

const uint8_t *airplay_srp_server_public_key(const AirPlaySrpServer *server)
{
    return server ? server->public_key : NULL;
}

static bool calculate_session_key(const mbedtls_mpi *shared_secret,
                                  uint8_t output[AIRPLAY_SRP_SESSION_KEY_SIZE])
{
    uint8_t shared_bytes[AIRPLAY_SRP_PUBLIC_KEY_SIZE];
    uint8_t counter[4] = {0};
    size_t shared_size;
    mbedtls_sha1_context hash;
    bool ok = false;

    if (!mpi_write_minimal(shared_secret, shared_bytes, &shared_size))
        return false;
    sha1_start(&hash);
    if (!sha1_update(&hash, shared_bytes, shared_size) ||
        !sha1_update(&hash, counter, sizeof(counter)) ||
        !sha1_finish(&hash, output))
        goto cleanup;
    counter[3] = 1;
    sha1_start(&hash);
    if (!sha1_update(&hash, shared_bytes, shared_size) ||
        !sha1_update(&hash, counter, sizeof(counter)) ||
        !sha1_finish(&hash, output + AIRPLAY_SRP_PROOF_SIZE))
        goto cleanup;
    ok = true;

cleanup:
    airplay_crypto_secure_zero(shared_bytes, sizeof(shared_bytes));
    return ok;
}

static bool calculate_client_proof(const AirPlaySrpServer *server,
                                   const mbedtls_mpi *client_public,
                                   uint8_t output[AIRPLAY_SRP_PROOF_SIZE])
{
    uint8_t modulus_hash[AIRPLAY_SRP_PROOF_SIZE];
    uint8_t generator_hash[AIRPLAY_SRP_PROOF_SIZE];
    uint8_t username_hash[AIRPLAY_SRP_PROOF_SIZE];
    uint8_t xor_hash[AIRPLAY_SRP_PROOF_SIZE];
    uint8_t mpi_bytes[AIRPLAY_SRP_PUBLIC_KEY_SIZE];
    size_t mpi_size;
    mbedtls_sha1_context hash;
    bool ok = false;

    if (!hash_mpi_minimal(&server->modulus, modulus_hash) ||
        !hash_mpi_minimal(&server->generator, generator_hash) ||
        !airplay_crypto_sha1(server->username, strlen(server->username), username_hash))
        goto cleanup;
    for (size_t i = 0; i < sizeof(xor_hash); ++i)
        xor_hash[i] = (uint8_t)(modulus_hash[i] ^ generator_hash[i]);
    sha1_start(&hash);
    if (!sha1_update(&hash, xor_hash, sizeof(xor_hash)) ||
        !sha1_update(&hash, username_hash, sizeof(username_hash)) ||
        !sha1_update(&hash, server->salt, sizeof(server->salt)) ||
        !mpi_write_minimal(client_public, mpi_bytes, &mpi_size) ||
        !sha1_update(&hash, mpi_bytes, mpi_size) ||
        !mpi_write_minimal(&server->server_public, mpi_bytes, &mpi_size) ||
        !sha1_update(&hash, mpi_bytes, mpi_size) ||
        !sha1_update(&hash, server->session_key, sizeof(server->session_key)) ||
        !sha1_finish(&hash, output))
        goto cleanup;
    ok = true;

cleanup:
    airplay_crypto_secure_zero(modulus_hash, sizeof(modulus_hash));
    airplay_crypto_secure_zero(generator_hash, sizeof(generator_hash));
    airplay_crypto_secure_zero(username_hash, sizeof(username_hash));
    airplay_crypto_secure_zero(xor_hash, sizeof(xor_hash));
    airplay_crypto_secure_zero(mpi_bytes, sizeof(mpi_bytes));
    return ok;
}

static bool calculate_server_proof(const mbedtls_mpi *client_public,
                                   const uint8_t client_proof[AIRPLAY_SRP_PROOF_SIZE],
                                   const uint8_t session_key[AIRPLAY_SRP_SESSION_KEY_SIZE],
                                   uint8_t output[AIRPLAY_SRP_PROOF_SIZE])
{
    uint8_t public_bytes[AIRPLAY_SRP_PUBLIC_KEY_SIZE];
    size_t public_size;
    mbedtls_sha1_context hash;

    if (!mpi_write_minimal(client_public, public_bytes, &public_size))
        return false;
    sha1_start(&hash);
    if (!sha1_update(&hash, public_bytes, public_size) ||
        !sha1_update(&hash, client_proof, AIRPLAY_SRP_PROOF_SIZE) ||
        !sha1_update(&hash, session_key, AIRPLAY_SRP_SESSION_KEY_SIZE) ||
        !sha1_finish(&hash, output))
    {
        airplay_crypto_secure_zero(public_bytes, sizeof(public_bytes));
        return false;
    }
    airplay_crypto_secure_zero(public_bytes, sizeof(public_bytes));
    return true;
}

bool airplay_srp_server_verify(AirPlaySrpServer *server,
                               const uint8_t *client_public_key,
                               size_t client_public_key_size,
                               const uint8_t client_proof[AIRPLAY_SRP_PROOF_SIZE],
                               uint8_t server_proof[AIRPLAY_SRP_PROOF_SIZE])
{
    mbedtls_mpi client_public;
    mbedtls_mpi check;
    mbedtls_mpi scrambling;
    mbedtls_mpi shared_secret;
    mbedtls_mpi temporary;
    mbedtls_mpi base;
    uint8_t expected_proof[AIRPLAY_SRP_PROOF_SIZE];
    bool ok = false;

    if (!server || server->authenticated || !client_public_key || client_public_key_size == 0 ||
        client_public_key_size > AIRPLAY_SRP_PUBLIC_KEY_SIZE || !client_proof || !server_proof)
        return false;
    mbedtls_mpi_init(&client_public);
    mbedtls_mpi_init(&check);
    mbedtls_mpi_init(&scrambling);
    mbedtls_mpi_init(&shared_secret);
    mbedtls_mpi_init(&temporary);
    mbedtls_mpi_init(&base);
    if (mbedtls_mpi_read_binary(&client_public, client_public_key, client_public_key_size) != 0 ||
        mbedtls_mpi_mod_mpi(&check, &client_public, &server->modulus) != 0 ||
        mbedtls_mpi_cmp_int(&check, 0) == 0 ||
        !hash_padded_pair(&client_public, &server->server_public, &scrambling) ||
        mbedtls_mpi_exp_mod(&temporary, &server->verifier, &scrambling,
                            &server->modulus, NULL) != 0 ||
        mbedtls_mpi_mul_mpi(&base, &client_public, &temporary) != 0 ||
        mbedtls_mpi_mod_mpi(&base, &base, &server->modulus) != 0 ||
        mbedtls_mpi_exp_mod(&shared_secret, &base, &server->private_key,
                            &server->modulus, NULL) != 0 ||
        !calculate_session_key(&shared_secret, server->session_key) ||
        !calculate_client_proof(server, &client_public, expected_proof) ||
        !airplay_crypto_equal(expected_proof, client_proof, sizeof(expected_proof)) ||
        !calculate_server_proof(&client_public, expected_proof, server->session_key,
                                server_proof))
        goto cleanup;
    server->authenticated = true;
    ok = true;

cleanup:
    if (!ok)
    {
        airplay_crypto_secure_zero(server->session_key, sizeof(server->session_key));
        airplay_crypto_secure_zero(server_proof, AIRPLAY_SRP_PROOF_SIZE);
    }
    airplay_crypto_secure_zero(expected_proof, sizeof(expected_proof));
    mbedtls_mpi_free(&base);
    mbedtls_mpi_free(&temporary);
    mbedtls_mpi_free(&shared_secret);
    mbedtls_mpi_free(&scrambling);
    mbedtls_mpi_free(&check);
    mbedtls_mpi_free(&client_public);
    return ok;
}

bool airplay_srp_server_session_key(
    const AirPlaySrpServer *server,
    uint8_t output[AIRPLAY_SRP_SESSION_KEY_SIZE])
{
    if (!server || !server->authenticated || !output)
        return false;
    memcpy(output, server->session_key, AIRPLAY_SRP_SESSION_KEY_SIZE);
    return true;
}
