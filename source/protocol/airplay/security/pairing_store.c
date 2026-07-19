#include "pairing_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAIRING_STORE_PATH_MAX 512u
#define PAIRING_STORE_ENTRY_SIZE (AIRPLAY_SRP_USERNAME_MAX + 1u + AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE)
#define PAIRING_STORE_HEADER_SIZE 16u
#define PAIRING_STORE_CHECKSUM_SIZE AIRPLAY_CRYPTO_SHA256_SIZE
#define PAIRING_STORE_RECORD_SIZE                                                        \
    (PAIRING_STORE_HEADER_SIZE + PAIRING_STORE_ENTRY_SIZE * AIRPLAY_PAIRING_STORE_MAX_CLIENTS + \
     PAIRING_STORE_CHECKSUM_SIZE)

typedef struct
{
    char username[AIRPLAY_SRP_USERNAME_MAX + 1u];
    uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE];
} AirPlayPairingStoreEntry;

struct AirPlayPairingStore
{
    char directory[PAIRING_STORE_PATH_MAX];
    AirPlayPairingStoreEntry entries[AIRPLAY_PAIRING_STORE_MAX_CLIENTS];
    size_t count;
};

static const uint8_t g_store_magic[8] = {'N', 'X', 'A', 'P', 'P', 'R', '0', '1'};

static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static bool make_path(char output[PAIRING_STORE_PATH_MAX],
                      const char *directory, const char *suffix)
{
    int written = snprintf(output, PAIRING_STORE_PATH_MAX, "%s/pairings.bin%s",
                           directory, suffix ? suffix : "");
    return written > 0 && (size_t)written < PAIRING_STORE_PATH_MAX;
}

static bool read_file(const char *path, uint8_t record[PAIRING_STORE_RECORD_SIZE])
{
    struct stat info;
    size_t offset = 0;
    int file = open(path, O_RDONLY);
    bool ok = file >= 0;

    if (!ok)
        return false;
    if (fstat(file, &info) != 0 || info.st_size != (off_t)PAIRING_STORE_RECORD_SIZE)
        ok = false;
    while (ok && offset < PAIRING_STORE_RECORD_SIZE)
    {
        ssize_t amount = read(file, record + offset, PAIRING_STORE_RECORD_SIZE - offset);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0)
        {
            ok = false;
            break;
        }
        offset += (size_t)amount;
    }
    if (close(file) != 0)
        ok = false;
    return ok;
}

static bool write_all(int file, const uint8_t *data, size_t size)
{
    size_t offset = 0;

    while (offset < size)
    {
        ssize_t amount = write(file, data + offset, size - offset);
        if (amount < 0 && errno == EINTR)
            continue;
        if (amount <= 0)
            return false;
        offset += (size_t)amount;
    }
    return true;
}

static bool encode_store(const AirPlayPairingStore *store,
                         uint8_t record[PAIRING_STORE_RECORD_SIZE])
{
    size_t offset = PAIRING_STORE_HEADER_SIZE;

    memset(record, 0, PAIRING_STORE_RECORD_SIZE);
    memcpy(record, g_store_magic, sizeof(g_store_magic));
    write_u32_be(record + 8, 1u);
    write_u32_be(record + 12, (uint32_t)store->count);
    for (size_t i = 0; i < store->count; ++i)
    {
        memcpy(record + offset, store->entries[i].username,
               sizeof(store->entries[i].username));
        offset += sizeof(store->entries[i].username);
        memcpy(record + offset, store->entries[i].public_key,
               sizeof(store->entries[i].public_key));
        offset += sizeof(store->entries[i].public_key);
    }
    return airplay_crypto_sha256(record, PAIRING_STORE_RECORD_SIZE - PAIRING_STORE_CHECKSUM_SIZE,
                                 record + PAIRING_STORE_RECORD_SIZE - PAIRING_STORE_CHECKSUM_SIZE);
}

static bool decode_store(const uint8_t record[PAIRING_STORE_RECORD_SIZE],
                         AirPlayPairingStore *store)
{
    uint8_t checksum[PAIRING_STORE_CHECKSUM_SIZE];
    uint32_t count;
    size_t offset = PAIRING_STORE_HEADER_SIZE;
    bool ok = false;

    if (!airplay_crypto_sha256(record, PAIRING_STORE_RECORD_SIZE - PAIRING_STORE_CHECKSUM_SIZE,
                               checksum) ||
        !airplay_crypto_equal(record, g_store_magic, sizeof(g_store_magic)) ||
        read_u32_be(record + 8) != 1u)
        goto cleanup;
    count = read_u32_be(record + 12);
    if (count > AIRPLAY_PAIRING_STORE_MAX_CLIENTS ||
        !airplay_crypto_equal(checksum,
                              record + PAIRING_STORE_RECORD_SIZE - PAIRING_STORE_CHECKSUM_SIZE,
                              sizeof(checksum)))
        goto cleanup;
    memset(store->entries, 0, sizeof(store->entries));
    for (size_t i = 0; i < count; ++i)
    {
        const void *terminator;
        memcpy(store->entries[i].username, record + offset,
               sizeof(store->entries[i].username));
        offset += sizeof(store->entries[i].username);
        terminator = memchr(store->entries[i].username, '\0',
                            sizeof(store->entries[i].username));
        if (!terminator || store->entries[i].username[0] == '\0')
            goto cleanup;
        memcpy(store->entries[i].public_key, record + offset,
               sizeof(store->entries[i].public_key));
        offset += sizeof(store->entries[i].public_key);
    }
    store->count = count;
    ok = true;

cleanup:
    airplay_crypto_secure_zero(checksum, sizeof(checksum));
    if (!ok)
    {
        airplay_crypto_secure_zero(store->entries, sizeof(store->entries));
        store->count = 0;
    }
    return ok;
}

static bool save_store(const AirPlayPairingStore *store)
{
    uint8_t record[PAIRING_STORE_RECORD_SIZE];
    char path[PAIRING_STORE_PATH_MAX];
    char temporary[PAIRING_STORE_PATH_MAX];
    int file = -1;
    bool ok = false;

    if (!make_path(path, store->directory, NULL) ||
        !make_path(temporary, store->directory, ".tmp") ||
        !encode_store(store, record))
        goto cleanup;
    file = open(temporary, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (file < 0)
        goto cleanup;
    ok = write_all(file, record, sizeof(record)) && fsync(file) == 0;
    if (close(file) != 0)
        ok = false;
    file = -1;
    if (ok)
    {
        int directory_file;

        chmod(temporary, 0600);
        if (rename(temporary, path) != 0)
            ok = false;
        directory_file = open(store->directory, O_RDONLY);
        if (ok && directory_file >= 0)
        {
            fsync(directory_file);
            close(directory_file);
        }
    }
    if (!ok)
        remove(temporary);

cleanup:
    if (file >= 0)
        close(file);
    airplay_crypto_secure_zero(record, sizeof(record));
    return ok;
}

bool airplay_pairing_store_open(const char *directory, AirPlayPairingStore **store_out)
{
    AirPlayPairingStore *store;
    uint8_t record[PAIRING_STORE_RECORD_SIZE];
    char path[PAIRING_STORE_PATH_MAX];
    char corrupt[PAIRING_STORE_PATH_MAX];
    size_t directory_size;

    if (!directory || !store_out || *store_out)
        return false;
    directory_size = strlen(directory);
    if (directory_size == 0 || directory_size >= PAIRING_STORE_PATH_MAX)
        return false;
    store = calloc(1, sizeof(*store));
    if (!store)
        return false;
    memcpy(store->directory, directory, directory_size + 1u);
    if (!make_path(path, directory, NULL))
        goto failure;
    if (access(path, F_OK) == 0)
    {
        if (!read_file(path, record) || !decode_store(record, store))
        {
            if (!make_path(corrupt, directory, ".corrupt"))
                goto failure;
            remove(corrupt);
            if (rename(path, corrupt) != 0)
                goto failure;
        }
    }
    airplay_crypto_secure_zero(record, sizeof(record));
    *store_out = store;
    return true;

failure:
    airplay_crypto_secure_zero(record, sizeof(record));
    airplay_pairing_store_close(store);
    return false;
}

void airplay_pairing_store_close(AirPlayPairingStore *store)
{
    if (!store)
        return;
    airplay_crypto_secure_zero(store, sizeof(*store));
    free(store);
}

bool airplay_pairing_store_contains(const AirPlayPairingStore *store,
                                    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE])
{
    bool found = false;

    if (!store || !public_key)
        return false;
    for (size_t i = 0; i < store->count; ++i)
    {
        bool equal = airplay_crypto_equal(store->entries[i].public_key, public_key,
                                          AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE);
        found = found || equal;
    }
    return found;
}

bool airplay_pairing_store_upsert(
    AirPlayPairingStore *store,
    const char *username,
    const uint8_t public_key[AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE])
{
    AirPlayPairingStoreEntry previous = {0};
    size_t username_size;
    size_t index = store ? store->count : 0;
    bool replacing = false;

    if (!store || !username || !public_key)
        return false;
    username_size = strlen(username);
    if (username_size == 0 || username_size > AIRPLAY_SRP_USERNAME_MAX)
        return false;
    for (size_t i = 0; i < store->count; ++i)
    {
        if (strcmp(store->entries[i].username, username) == 0 ||
            airplay_crypto_equal(store->entries[i].public_key, public_key,
                                 AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE))
        {
            index = i;
            replacing = true;
            break;
        }
    }
    if (!replacing && store->count >= AIRPLAY_PAIRING_STORE_MAX_CLIENTS)
        return false;
    if (replacing)
        previous = store->entries[index];
    memset(&store->entries[index], 0, sizeof(store->entries[index]));
    memcpy(store->entries[index].username, username, username_size + 1u);
    memcpy(store->entries[index].public_key, public_key,
           AIRPLAY_CRYPTO_ED25519_PUBLIC_SIZE);
    if (!replacing)
        store->count++;
    if (!save_store(store))
    {
        if (replacing)
            store->entries[index] = previous;
        else
        {
            airplay_crypto_secure_zero(&store->entries[index], sizeof(store->entries[index]));
            store->count--;
        }
        return false;
    }
    airplay_crypto_secure_zero(&previous, sizeof(previous));
    return true;
}
