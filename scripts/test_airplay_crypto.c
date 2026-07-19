#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "protocol/airplay/security/crypto.h"
#include "protocol/airplay/security/identity.h"

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
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static bool decode_hex(const char *text, uint8_t *output, size_t output_size)
{
    if (!text || strlen(text) != output_size * 2)
        return false;
    for (size_t i = 0; i < output_size; ++i)
    {
        int high = hex_digit(text[i * 2]);
        int low = hex_digit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void test_hash_and_kdf_vectors(void)
{
    uint8_t expected_sha256[32];
    uint8_t expected_sha512[64];
    uint8_t expected_sha1[20];
    uint8_t digest[64];
    uint8_t hmac_key[20];
    uint8_t expected_hmac[32];
    uint8_t salt[13];
    uint8_t ikm[22];
    uint8_t info[10];
    uint8_t expected_hkdf[42];
    uint8_t hkdf[42];

    CHECK(decode_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                     expected_sha256, sizeof(expected_sha256)));
    CHECK(airplay_crypto_sha256("abc", 3, digest));
    CHECK(airplay_crypto_equal(digest, expected_sha256, sizeof(expected_sha256)));

    CHECK(decode_hex("ddaf35a193617abacc417349ae204131"
                     "12e6fa4e89a97ea20a9eeee64b55d39a"
                     "2192992a274fc1a836ba3c23a3feebbd"
                     "454d4423643ce80e2a9ac94fa54ca49f",
                     expected_sha512, sizeof(expected_sha512)));
    CHECK(airplay_crypto_sha512("abc", 3, digest));
    CHECK(airplay_crypto_equal(digest, expected_sha512, sizeof(expected_sha512)));

    CHECK(decode_hex("a9993e364706816aba3e25717850c26c9cd0d89d",
                     expected_sha1, sizeof(expected_sha1)));
    CHECK(airplay_crypto_sha1("abc", 3, digest));
    CHECK(airplay_crypto_equal(digest, expected_sha1, sizeof(expected_sha1)));

    memset(hmac_key, 0x0b, sizeof(hmac_key));
    CHECK(decode_hex("b0344c61d8db38535ca8afceaf0bf12b"
                     "881dc200c9833da726e9376c2e32cff7",
                     expected_hmac, sizeof(expected_hmac)));
    CHECK(airplay_crypto_hmac_sha256(hmac_key, sizeof(hmac_key),
                                     "Hi There", 8, digest));
    CHECK(airplay_crypto_equal(digest, expected_hmac, sizeof(expected_hmac)));

    memset(ikm, 0x0b, sizeof(ikm));
    for (size_t i = 0; i < sizeof(salt); ++i)
        salt[i] = (uint8_t)i;
    for (size_t i = 0; i < sizeof(info); ++i)
        info[i] = (uint8_t)(0xf0u + i);
    CHECK(decode_hex("3cb25f25faacd57a90434f64d0362f2a"
                     "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                     "34007208d5b887185865",
                     expected_hkdf, sizeof(expected_hkdf)));
    CHECK(airplay_crypto_hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm),
                                     info, sizeof(info), hkdf, sizeof(hkdf)));
    CHECK(airplay_crypto_equal(hkdf, expected_hkdf, sizeof(hkdf)));
}

static void test_x25519_vectors(void)
{
    AirPlayCryptoRng rng = {0};
    uint8_t alice_private[32];
    uint8_t alice_public[32];
    uint8_t expected_alice_public[32];
    uint8_t bob_private[32];
    uint8_t bob_public[32];
    uint8_t expected_bob_public[32];
    uint8_t shared[32];
    uint8_t expected_shared[32];
    uint8_t low_order[32] = {0};

    CHECK(decode_hex("77076d0a7318a57d3c16c17251b26645"
                     "df4c2f87ebc0992ab177fba51db92c2a",
                     alice_private, sizeof(alice_private)));
    CHECK(decode_hex("8520f0098930a754748b7ddcb43ef75a"
                     "0dbf3a0d26381af4eba4a98eaa9b4e6a",
                     expected_alice_public, sizeof(expected_alice_public)));
    CHECK(decode_hex("5dab087e624a8a4b79e17f8b83800ee6"
                     "6f3bb1292618b6fd1c2f8b27ff88e0eb",
                     bob_private, sizeof(bob_private)));
    CHECK(decode_hex("de9edb7d7b7dc1b4d35b61c2ece43537"
                     "3f8343c85b78674dadfc7e146f882b4f",
                     expected_bob_public, sizeof(expected_bob_public)));
    CHECK(decode_hex("4a5d9d5ba4ce2de1728e3bf480350f25"
                     "e07e21c947d19e3376f09b3c1e161742",
                     expected_shared, sizeof(expected_shared)));

    CHECK(airplay_crypto_rng_init(&rng, "NX-Cast X25519 vector test"));
    CHECK(airplay_crypto_x25519_public(&rng, alice_private, alice_public));
    CHECK(airplay_crypto_equal(alice_public, expected_alice_public, sizeof(alice_public)));
    CHECK(airplay_crypto_x25519_public(&rng, bob_private, bob_public));
    CHECK(airplay_crypto_equal(bob_public, expected_bob_public, sizeof(bob_public)));
    CHECK(airplay_crypto_x25519_shared(&rng, alice_private, bob_public, shared));
    CHECK(airplay_crypto_equal(shared, expected_shared, sizeof(shared)));
    CHECK(!airplay_crypto_x25519_shared(&rng, alice_private, low_order, shared));
    airplay_crypto_rng_deinit(&rng);
}

static void test_ed25519_vector(void)
{
    uint8_t seed[32];
    uint8_t expected_public[32];
    uint8_t expected_signature[64];
    uint8_t public_key[32];
    uint8_t signature[64];

    CHECK(airplay_crypto_ed25519_available());
    CHECK(decode_hex("9d61b19deffd5a60ba844af492ec2cc4"
                     "4449c5697b326919703bac031cae7f60",
                     seed, sizeof(seed)));
    CHECK(decode_hex("d75a980182b10ab7d54bfed3c964073a"
                     "0ee172f3daa62325af021a68f707511a",
                     expected_public, sizeof(expected_public)));
    CHECK(decode_hex("e5564300c360ac729086e2cc806e828a"
                     "84877f1eb8e5d974d873e06522490155"
                     "5fb8821590a33bacc61e39701cf9b46b"
                     "d25bf5f0595bbe24655141438e7a100b",
                     expected_signature, sizeof(expected_signature)));

    CHECK(airplay_crypto_ed25519_public(seed, public_key));
    CHECK(airplay_crypto_equal(public_key, expected_public, sizeof(public_key)));
    CHECK(airplay_crypto_ed25519_sign(seed, NULL, 0, signature));
    CHECK(airplay_crypto_equal(signature, expected_signature, sizeof(signature)));
    CHECK(airplay_crypto_ed25519_verify(public_key, NULL, 0, signature));
    signature[0] ^= 1u;
    CHECK(!airplay_crypto_ed25519_verify(public_key, NULL, 0, signature));
}

static void test_aes_ctr_vector(void)
{
    uint8_t key[16];
    uint8_t counter[16];
    uint8_t plaintext[64];
    uint8_t expected[64];
    uint8_t ciphertext[64];
    uint8_t recovered[64];
    AirPlayCryptoAesCtr encrypt = {0};
    AirPlayCryptoAesCtr decrypt = {0};

    CHECK(decode_hex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)));
    CHECK(decode_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", counter, sizeof(counter)));
    CHECK(decode_hex("6bc1bee22e409f96e93d7e117393172a"
                     "ae2d8a571e03ac9c9eb76fac45af8e51"
                     "30c81c46a35ce411e5fbc1191a0a52ef"
                     "f69f2445df4f9b17ad2b417be66c3710",
                     plaintext, sizeof(plaintext)));
    CHECK(decode_hex("874d6191b620e3261bef6864990db6ce"
                     "9806f66b7970fdff8617187bb9fffdff"
                     "5ae4df3edbd5d35e5b4f09020db03eab"
                     "1e031dda2fbe03d1792170a0f3009cee",
                     expected, sizeof(expected)));

    CHECK(airplay_crypto_aes_ctr_init(&encrypt, key, sizeof(key), counter));
    CHECK(airplay_crypto_aes_ctr_crypt(&encrypt, plaintext, ciphertext, 7));
    CHECK(airplay_crypto_aes_ctr_crypt(&encrypt, plaintext + 7, ciphertext + 7,
                                       sizeof(plaintext) - 7));
    CHECK(airplay_crypto_equal(ciphertext, expected, sizeof(expected)));
    airplay_crypto_aes_ctr_deinit(&encrypt);

    CHECK(airplay_crypto_aes_ctr_init(&decrypt, key, sizeof(key), counter));
    CHECK(airplay_crypto_aes_ctr_crypt(&decrypt, ciphertext, recovered, sizeof(recovered)));
    CHECK(airplay_crypto_equal(recovered, plaintext, sizeof(plaintext)));
    airplay_crypto_aes_ctr_deinit(&decrypt);
}

static void test_aes_cbc_vector(void)
{
    uint8_t key[16];
    uint8_t iv[16];
    uint8_t ciphertext[32];
    uint8_t expected[32];
    uint8_t plaintext[32];

    CHECK(decode_hex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)));
    CHECK(decode_hex("000102030405060708090a0b0c0d0e0f", iv, sizeof(iv)));
    CHECK(decode_hex("7649abac8119b246cee98e9b12e9197d"
                     "5086cb9b507219ee95db113a917678b2",
                     ciphertext, sizeof(ciphertext)));
    CHECK(decode_hex("6bc1bee22e409f96e93d7e117393172a"
                     "ae2d8a571e03ac9c9eb76fac45af8e51",
                     expected, sizeof(expected)));
    CHECK(airplay_crypto_aes_cbc_decrypt(key, iv, ciphertext, plaintext,
                                         sizeof(plaintext)));
    CHECK(airplay_crypto_equal(plaintext, expected, sizeof(expected)));
    CHECK(!airplay_crypto_aes_cbc_decrypt(key, iv, ciphertext, plaintext, 17u));
}

static void test_chachapoly_vector(void)
{
    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t aad[12];
    uint8_t plaintext[114];
    uint8_t expected_ciphertext[114];
    uint8_t expected_tag[16];
    uint8_t ciphertext[114];
    uint8_t tag[16];
    uint8_t recovered[114];

    CHECK(decode_hex("808182838485868788898a8b8c8d8e8f"
                     "909192939495969798999a9b9c9d9e9f",
                     key, sizeof(key)));
    CHECK(decode_hex("070000004041424344454647", nonce, sizeof(nonce)));
    CHECK(decode_hex("50515253c0c1c2c3c4c5c6c7", aad, sizeof(aad)));
    CHECK(decode_hex("4c616469657320616e642047656e746c"
                     "656d656e206f662074686520636c6173"
                     "73206f66202739393a20496620492063"
                     "6f756c64206f6666657220796f75206f"
                     "6e6c79206f6e652074697020666f7220"
                     "746865206675747572652c2073756e73"
                     "637265656e20776f756c642062652069"
                     "742e",
                     plaintext, sizeof(plaintext)));
    CHECK(decode_hex("d31a8d34648e60db7b86afbc53ef7ec2"
                     "a4aded51296e08fea9e2b5a736ee62d6"
                     "3dbea45e8ca9671282fafb69da92728b"
                     "1a71de0a9e060b2905d6a5b67ecd3b36"
                     "92ddbd7f2d778b8c9803aee328091b58"
                     "fab324e4fad675945585808b4831d7bc"
                     "3ff4def08e4b7a9de576d26586cec64b"
                     "6116",
                     expected_ciphertext, sizeof(expected_ciphertext)));
    CHECK(decode_hex("1ae10b594f09e26a7e902ecbd0600691",
                     expected_tag, sizeof(expected_tag)));

    CHECK(airplay_crypto_chachapoly_encrypt(key, nonce, aad, sizeof(aad),
                                            plaintext, sizeof(plaintext),
                                            ciphertext, tag));
    CHECK(airplay_crypto_equal(ciphertext, expected_ciphertext, sizeof(ciphertext)));
    CHECK(airplay_crypto_equal(tag, expected_tag, sizeof(tag)));
    CHECK(airplay_crypto_chachapoly_decrypt(key, nonce, aad, sizeof(aad), tag,
                                            ciphertext, sizeof(ciphertext), recovered));
    CHECK(airplay_crypto_equal(recovered, plaintext, sizeof(plaintext)));

    tag[0] ^= 1u;
    memset(recovered, 0xa5, sizeof(recovered));
    CHECK(!airplay_crypto_chachapoly_decrypt(key, nonce, aad, sizeof(aad), tag,
                                             ciphertext, sizeof(ciphertext), recovered));
    for (size_t i = 0; i < sizeof(recovered); ++i)
        CHECK(recovered[i] == 0);
}

static void test_aes_gcm_vector(void)
{
    uint8_t key[16] = {0};
    uint8_t nonce[12] = {0};
    uint8_t plaintext[16] = {0};
    uint8_t expected_ciphertext[16];
    uint8_t expected_tag[16];
    uint8_t ciphertext[16];
    uint8_t tag[16];
    uint8_t recovered[16];

    CHECK(decode_hex("0388dace60b6a392f328c2b971b2fe78",
                     expected_ciphertext, sizeof(expected_ciphertext)));
    CHECK(decode_hex("ab6e47d42cec13bdf53a67b21257bddf",
                     expected_tag, sizeof(expected_tag)));
    CHECK(airplay_crypto_aes_gcm_encrypt(key, nonce, sizeof(nonce), NULL, 0,
                                         plaintext, sizeof(plaintext), ciphertext, tag));
    CHECK(airplay_crypto_equal(ciphertext, expected_ciphertext, sizeof(ciphertext)));
    CHECK(airplay_crypto_equal(tag, expected_tag, sizeof(tag)));
    CHECK(airplay_crypto_aes_gcm_decrypt(key, nonce, sizeof(nonce), NULL, 0, tag,
                                         ciphertext, sizeof(ciphertext), recovered));
    CHECK(airplay_crypto_equal(recovered, plaintext, sizeof(plaintext)));
    tag[0] ^= 1u;
    CHECK(!airplay_crypto_aes_gcm_decrypt(key, nonce, sizeof(nonce), NULL, 0, tag,
                                          ciphertext, sizeof(ciphertext), recovered));
}

static bool write_byte_at(const char *path, long offset, uint8_t value)
{
    FILE *file = fopen(path, "r+b");
    bool ok;

    if (!file)
        return false;
    ok = fseek(file, offset, SEEK_SET) == 0 && fwrite(&value, 1, 1, file) == 1;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static void test_rng_and_identity(void)
{
    char temporary[] = "/tmp/nxcast-airplay-identity-XXXXXX";
    char identity_path[sizeof(temporary) + sizeof("/identity.bin")];
    char corrupt_path[sizeof(temporary) + sizeof("/identity.bin.corrupt")];
    char blocked_path[sizeof(temporary) + sizeof("/not-a-directory")];
    char blocked_child[sizeof(blocked_path) + sizeof("/airplay")];
    char device_id[18];
    uint8_t random_a[32];
    uint8_t random_b[32];
    uint8_t fingerprint_a[32];
    uint8_t fingerprint_b[32];
    uint8_t public_key[32];
    uint8_t signature[64];
    AirPlayCryptoRng rng = {0};
    AirPlayIdentity *first = NULL;
    AirPlayIdentity *second = NULL;
    AirPlayIdentity *recovered = NULL;
    AirPlayIdentity *blocked = NULL;
    AirPlayIdentityLoadResult result;
    int file;

    CHECK(mkdtemp(temporary) != NULL);
    CHECK(airplay_crypto_rng_init(&rng, "NX-Cast AirPlay host test"));
    CHECK(airplay_crypto_random(&rng, random_a, sizeof(random_a)));
    CHECK(airplay_crypto_random(&rng, random_b, sizeof(random_b)));
    CHECK(!airplay_crypto_equal(random_a, random_b, sizeof(random_a)));

    CHECK(airplay_identity_load_or_create(temporary, &rng, &first, &result));
    CHECK(result == AIRPLAY_IDENTITY_CREATED);
    CHECK(airplay_identity_fingerprint(first, fingerprint_a));
    CHECK(airplay_identity_device_id_string(first, device_id, sizeof(device_id)));
    CHECK(strlen(device_id) == 17);
    CHECK(airplay_identity_public_key(first, public_key));
    CHECK(airplay_identity_sign(first, (const uint8_t *)"identity", 8, signature));
    CHECK(airplay_crypto_ed25519_verify(public_key, (const uint8_t *)"identity", 8,
                                        signature));
    airplay_identity_destroy(first);
    first = NULL;

    CHECK(airplay_identity_load_or_create(temporary, &rng, &second, &result));
    CHECK(result == AIRPLAY_IDENTITY_LOADED);
    CHECK(airplay_identity_fingerprint(second, fingerprint_b));
    CHECK(airplay_crypto_equal(fingerprint_a, fingerprint_b, sizeof(fingerprint_a)));
    airplay_identity_destroy(second);
    second = NULL;

    snprintf(identity_path, sizeof(identity_path), "%s/identity.bin", temporary);
    snprintf(corrupt_path, sizeof(corrupt_path), "%s/identity.bin.corrupt", temporary);
    CHECK(write_byte_at(identity_path, 20, 0x5a));
    CHECK(airplay_identity_load_or_create(temporary, &rng, &recovered, &result));
    CHECK(result == AIRPLAY_IDENTITY_RECOVERED);
    CHECK(access(corrupt_path, F_OK) == 0);
    CHECK(airplay_identity_fingerprint(recovered, fingerprint_b));
    CHECK(!airplay_crypto_equal(fingerprint_a, fingerprint_b, sizeof(fingerprint_a)));
    airplay_identity_destroy(recovered);
    recovered = NULL;

    snprintf(blocked_path, sizeof(blocked_path), "%s/not-a-directory", temporary);
    snprintf(blocked_child, sizeof(blocked_child), "%s/airplay", blocked_path);
    file = open(blocked_path, O_CREAT | O_WRONLY, 0600);
    CHECK(file >= 0);
    if (file >= 0)
        close(file);
    CHECK(!airplay_identity_load_or_create(blocked_child, &rng, &blocked, &result));
    CHECK(blocked == NULL);

    airplay_crypto_rng_deinit(&rng);
    remove(blocked_path);
    remove(corrupt_path);
    remove(identity_path);
    CHECK(rmdir(temporary) == 0);
}

int main(void)
{
    test_hash_and_kdf_vectors();
    test_x25519_vectors();
    test_ed25519_vector();
    test_aes_ctr_vector();
    test_aes_cbc_vector();
    test_chachapoly_vector();
    test_aes_gcm_vector();
    test_rng_and_identity();

    if (g_failures != 0)
    {
        fprintf(stderr, "airplay crypto tests failed: %d\n", g_failures);
        return 1;
    }
    printf("airplay crypto tests passed\n");
    return 0;
}
