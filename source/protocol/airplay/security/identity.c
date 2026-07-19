#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define AIRPLAY_IDENTITY_PATH_MAX 512u
#define AIRPLAY_IDENTITY_FILE_NAME "identity.bin"
#define AIRPLAY_IDENTITY_MAGIC_SIZE 8u
#define AIRPLAY_IDENTITY_SEED_SIZE 32u
#define AIRPLAY_IDENTITY_RECORD_SIZE 84u
#define AIRPLAY_IDENTITY_CHECKSUM_OFFSET 52u

static const uint8_t g_identity_magic[AIRPLAY_IDENTITY_MAGIC_SIZE] = {
    'N', 'X', 'A', 'P', 'I', 'D', '0', '1'};

struct AirPlayIdentity
{
    uint8_t device_id[AIRPLAY_IDENTITY_DEVICE_ID_SIZE];
    uint8_t pairing_seed[AIRPLAY_IDENTITY_SEED_SIZE];
};

static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) |
           ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) |
           (uint32_t)input[3];
}

static bool ensure_directory(const char *directory)
{
    char path[AIRPLAY_IDENTITY_PATH_MAX];
    size_t length;

    if (!directory)
        return false;
    length = strlen(directory);
    if (length == 0 || length >= sizeof(path))
        return false;
    memcpy(path, directory, length + 1);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';

    for (size_t i = 1; i < length; ++i)
    {
        struct stat info;

        if (path[i] != '/' || path[i - 1] == ':')
            continue;
        path[i] = '\0';
        if ((mkdir(path, 0700) != 0 && errno != EEXIST) ||
            stat(path, &info) != 0 || !S_ISDIR(info.st_mode))
        {
            path[i] = '/';
            return false;
        }
        path[i] = '/';
    }

    if (mkdir(path, 0700) == 0)
        return true;
    if (errno == EEXIST)
    {
        struct stat info;
        return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
    }
    return false;
}

static bool make_path(char output[AIRPLAY_IDENTITY_PATH_MAX],
                      const char *directory, const char *suffix)
{
    int written;

    written = snprintf(output, AIRPLAY_IDENTITY_PATH_MAX, "%s/%s%s",
                       directory, AIRPLAY_IDENTITY_FILE_NAME, suffix ? suffix : "");
    return written > 0 && (size_t)written < AIRPLAY_IDENTITY_PATH_MAX;
}

static bool read_record(const char *path, uint8_t record[AIRPLAY_IDENTITY_RECORD_SIZE])
{
    struct stat info;
    size_t offset = 0;
    int file;
    bool ok = true;

    file = open(path, O_RDONLY);
    if (file < 0)
        return false;
    if (fstat(file, &info) != 0 || info.st_size != (off_t)AIRPLAY_IDENTITY_RECORD_SIZE)
        ok = false;
    while (ok && offset < AIRPLAY_IDENTITY_RECORD_SIZE)
    {
        ssize_t read_size = read(file, record + offset, AIRPLAY_IDENTITY_RECORD_SIZE - offset);
        if (read_size < 0 && errno == EINTR)
            continue;
        if (read_size <= 0)
        {
            ok = false;
            break;
        }
        offset += (size_t)read_size;
    }
    if (close(file) != 0)
        ok = false;
    return ok;
}

static bool decode_record(const uint8_t record[AIRPLAY_IDENTITY_RECORD_SIZE],
                          AirPlayIdentity *identity)
{
    uint8_t checksum[AIRPLAY_CRYPTO_SHA256_SIZE];
    bool valid;

    if (!airplay_crypto_sha256(record, AIRPLAY_IDENTITY_RECORD_SIZE - sizeof(checksum), checksum))
        return false;
    valid = airplay_crypto_equal(record, g_identity_magic, sizeof(g_identity_magic)) &&
            read_u32_be(record + 8) == 1u &&
            record[18] == 0 && record[19] == 0 &&
            airplay_crypto_equal(checksum, record + AIRPLAY_IDENTITY_CHECKSUM_OFFSET,
                                 sizeof(checksum));
    airplay_crypto_secure_zero(checksum, sizeof(checksum));
    if (!valid)
        return false;
    memcpy(identity->device_id, record + 12, sizeof(identity->device_id));
    memcpy(identity->pairing_seed, record + 20, sizeof(identity->pairing_seed));
    return true;
}

static bool encode_record(const AirPlayIdentity *identity,
                          uint8_t record[AIRPLAY_IDENTITY_RECORD_SIZE])
{
    memset(record, 0, AIRPLAY_IDENTITY_RECORD_SIZE);
    memcpy(record, g_identity_magic, sizeof(g_identity_magic));
    write_u32_be(record + 8, 1u);
    memcpy(record + 12, identity->device_id, sizeof(identity->device_id));
    memcpy(record + 20, identity->pairing_seed, sizeof(identity->pairing_seed));
    return airplay_crypto_sha256(record, AIRPLAY_IDENTITY_CHECKSUM_OFFSET,
                                 record + AIRPLAY_IDENTITY_CHECKSUM_OFFSET);
}

static bool write_all(int file, const uint8_t *data, size_t size)
{
    size_t offset = 0;

    while (offset < size)
    {
        ssize_t written = write(file, data + offset, size - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        offset += (size_t)written;
    }
    return true;
}

static bool write_record_atomic(const char *directory,
                                const uint8_t record[AIRPLAY_IDENTITY_RECORD_SIZE])
{
    char path[AIRPLAY_IDENTITY_PATH_MAX];
    char temporary[AIRPLAY_IDENTITY_PATH_MAX];
    int file = -1;
    bool ok = false;

    if (!make_path(path, directory, NULL) || !make_path(temporary, directory, ".tmp"))
        return false;
    file = open(temporary, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (file < 0)
        return false;
    ok = write_all(file, record, AIRPLAY_IDENTITY_RECORD_SIZE) && fsync(file) == 0;
    if (close(file) != 0)
        ok = false;
    if (ok)
    {
        chmod(temporary, 0600);
        if (rename(temporary, path) != 0)
            ok = false;
        else
        {
            int directory_fd = open(directory, O_RDONLY);
            if (directory_fd >= 0)
            {
                fsync(directory_fd);
                close(directory_fd);
            }
        }
    }
    if (!ok)
        remove(temporary);
    return ok;
}

static bool create_identity(AirPlayIdentity *identity, AirPlayCryptoRng *rng)
{
    if (!airplay_crypto_random(rng, identity->device_id, sizeof(identity->device_id)) ||
        !airplay_crypto_random(rng, identity->pairing_seed, sizeof(identity->pairing_seed)))
        return false;
    identity->device_id[0] = (uint8_t)((identity->device_id[0] | 0x02u) & 0xfeu);
    return true;
}

bool airplay_identity_load_or_create(const char *directory,
                                     AirPlayCryptoRng *rng,
                                     AirPlayIdentity **identity,
                                     AirPlayIdentityLoadResult *load_result)
{
    AirPlayIdentity *candidate = NULL;
    uint8_t record[AIRPLAY_IDENTITY_RECORD_SIZE];
    char path[AIRPLAY_IDENTITY_PATH_MAX];
    char corrupt_path[AIRPLAY_IDENTITY_PATH_MAX];
    bool existed;
    bool corrupted = false;

    if (!directory || !rng || !rng->state || !identity || *identity || !load_result ||
        !ensure_directory(directory) || !make_path(path, directory, NULL))
        return false;
    candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return false;

    existed = access(path, F_OK) == 0;
    if (existed && read_record(path, record) && decode_record(record, candidate))
    {
        airplay_crypto_secure_zero(record, sizeof(record));
        *identity = candidate;
        *load_result = AIRPLAY_IDENTITY_LOADED;
        return true;
    }

    if (existed)
    {
        corrupted = true;
        if (!make_path(corrupt_path, directory, ".corrupt"))
            goto failure;
        remove(corrupt_path);
        if (rename(path, corrupt_path) != 0)
            goto failure;
    }
    if (!create_identity(candidate, rng) || !encode_record(candidate, record) ||
        !write_record_atomic(directory, record))
        goto failure;

    airplay_crypto_secure_zero(record, sizeof(record));
    *identity = candidate;
    *load_result = corrupted ? AIRPLAY_IDENTITY_RECOVERED : AIRPLAY_IDENTITY_CREATED;
    return true;

failure:
    airplay_crypto_secure_zero(record, sizeof(record));
    airplay_identity_destroy(candidate);
    return false;
}

void airplay_identity_destroy(AirPlayIdentity *identity)
{
    if (!identity)
        return;
    airplay_crypto_secure_zero(identity, sizeof(*identity));
    free(identity);
}

bool airplay_identity_device_id(const AirPlayIdentity *identity,
                                uint8_t output[AIRPLAY_IDENTITY_DEVICE_ID_SIZE])
{
    if (!identity || !output)
        return false;
    memcpy(output, identity->device_id, AIRPLAY_IDENTITY_DEVICE_ID_SIZE);
    return true;
}

bool airplay_identity_device_id_string(const AirPlayIdentity *identity,
                                       char *output, size_t output_size)
{
    int written;

    if (!identity || !output || output_size < 18)
        return false;
    written = snprintf(output, output_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                       identity->device_id[0], identity->device_id[1],
                       identity->device_id[2], identity->device_id[3],
                       identity->device_id[4], identity->device_id[5]);
    return written == 17;
}

bool airplay_identity_fingerprint(const AirPlayIdentity *identity,
                                  uint8_t output[AIRPLAY_IDENTITY_FINGERPRINT_SIZE])
{
    if (!identity || !output)
        return false;
    return airplay_crypto_sha256(identity->pairing_seed, sizeof(identity->pairing_seed), output);
}

bool airplay_identity_public_key(
    const AirPlayIdentity *identity,
    uint8_t output[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE])
{
    if (!identity || !output)
        return false;
    return airplay_crypto_ed25519_public(identity->pairing_seed, output);
}

bool airplay_identity_sign(
    const AirPlayIdentity *identity,
    const uint8_t *message, size_t message_size,
    uint8_t signature[AIRPLAY_CRYPTO_ED25519_SIGNATURE_SIZE])
{
    if (!identity)
        return false;
    return airplay_crypto_ed25519_sign(identity->pairing_seed, message,
                                       message_size, signature);
}
