#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol/airplay/protocol/plist.h"

#define FIXTURE_ROOT "scripts/fixtures/airplay/plist/"

static int hex_value(int character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static uint8_t *load_hex_fixture(const char *name, size_t *length_out)
{
    char path[256];
    FILE *file;
    uint8_t *bytes = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int high = -1;
    int character;

    assert(name != NULL);
    assert(length_out != NULL);
    snprintf(path, sizeof(path), "%s%s", FIXTURE_ROOT, name);
    file = fopen(path, "rb");
    assert(file != NULL);
    while ((character = fgetc(file)) != EOF)
    {
        int value;
        if (isspace((unsigned char)character))
            continue;
        value = hex_value(character);
        assert(value >= 0);
        if (high < 0)
        {
            high = value;
            continue;
        }
        if (length == capacity)
        {
            size_t next_capacity = capacity ? capacity * 2U : 128U;
            uint8_t *next = realloc(bytes, next_capacity);
            assert(next != NULL);
            bytes = next;
            capacity = next_capacity;
        }
        bytes[length++] = (uint8_t)((high << 4) | value);
        high = -1;
    }
    fclose(file);
    assert(high < 0);
    assert(length > 0);
    *length_out = length;
    return bytes;
}

static void dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value)
{
    assert(value != NULL);
    assert(airplay_plist_dict_set(dict, key, value));
}

static uint64_t dict_uint(const AirPlayPlistValue *dict, const char *key)
{
    uint64_t value = 0;
    assert(airplay_plist_get_uint(airplay_plist_dict_get(dict, key), &value));
    return value;
}

static bool dict_bool(const AirPlayPlistValue *dict, const char *key)
{
    bool value = false;
    assert(airplay_plist_get_bool(airplay_plist_dict_get(dict, key), &value));
    return value;
}

static void test_info_fixture(void)
{
    size_t fixture_length;
    uint8_t *fixture = load_hex_fixture("info-response.bplist.hex", &fixture_length);
    AirPlayPlistValue *root = NULL;
    AirPlayPlistError error = AIRPLAY_PLIST_OK;
    const AirPlayPlistValue *streams;
    const AirPlayPlistValue *stream;
    const AirPlayPlistValue *public_key;
    const uint8_t expected_key[] = {1, 2, 3, 4};
    const uint8_t *key_bytes;
    size_t key_length = 0;
    double volume = 0.0;

    assert(airplay_plist_decode(fixture, fixture_length, &root, &error));
    assert(error == AIRPLAY_PLIST_OK);
    assert(airplay_plist_type(root) == AIRPLAY_PLIST_TYPE_DICT);
    assert(strcmp(airplay_plist_get_string(airplay_plist_dict_get(root, "name")), "NX-Cast") == 0);
    assert(dict_uint(root, "features") == 123456789U);
    assert(dict_bool(root, "readyToPlay"));
    assert(!dict_bool(root, "overscanned"));
    assert(airplay_plist_get_real(airplay_plist_dict_get(root, "initialVolume"), &volume));
    assert(volume == -10.5);

    public_key = airplay_plist_dict_get(root, "pk");
    key_bytes = airplay_plist_get_data(public_key, &key_length);
    assert(key_length == sizeof(expected_key));
    assert(memcmp(key_bytes, expected_key, sizeof(expected_key)) == 0);

    streams = airplay_plist_dict_get(root, "streams");
    assert(airplay_plist_array_size(streams) == 1);
    stream = airplay_plist_array_get(streams, 0);
    assert(dict_uint(stream, "type") == 110);
    assert(dict_bool(stream, "usingScreen"));
    assert(airplay_plist_array_get(streams, 1) == NULL);

    airplay_plist_free(root);
    free(fixture);
}

static AirPlayPlistValue *build_round_trip_tree(void)
{
    static const uint8_t key[] = {0xde, 0xad, 0xbe, 0xef};
    static const char unicode_name[] = "NX-Cast \xe5\xae\xa2\xe5\x8e\x85 \xf0\x9f\x93\xba";
    AirPlayPlistValue *root = airplay_plist_new_dict();
    AirPlayPlistValue *numbers = airplay_plist_new_array();
    size_t index;

    assert(root != NULL);
    assert(numbers != NULL);
    dict_set(root, "name", airplay_plist_new_string(unicode_name));
    dict_set(root, "enabled", airplay_plist_new_bool(true));
    dict_set(root, "disabled", airplay_plist_new_bool(false));
    dict_set(root, "identifier", airplay_plist_new_uint(UINT64_C(0xfedcba9876543210)));
    dict_set(root, "volume", airplay_plist_new_real(0.75));
    dict_set(root, "key", airplay_plist_new_data(key, sizeof(key)));
    dict_set(root, "empty", airplay_plist_new_data(NULL, 0));
    for (index = 0; index < 20; ++index)
        assert(airplay_plist_array_append(numbers, airplay_plist_new_uint(index * index)));
    dict_set(root, "numbers", numbers);
    return root;
}

static void test_round_trip(void)
{
    static const char expected_name[] = "NX-Cast \xe5\xae\xa2\xe5\x8e\x85 \xf0\x9f\x93\xba";
    AirPlayPlistValue *source = build_round_trip_tree();
    AirPlayPlistValue *decoded = NULL;
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    size_t empty_length = 99;
    AirPlayPlistError error = AIRPLAY_PLIST_OK;
    const AirPlayPlistValue *numbers;
    FILE *artifact;
    size_t index;

    assert(airplay_plist_encode(source, &encoded, &encoded_length, &error));
    assert(error == AIRPLAY_PLIST_OK);
    assert(encoded_length > 40);
    artifact = fopen("build/tests/airplay-plist-roundtrip.bplist", "wb");
    assert(artifact != NULL);
    assert(fwrite(encoded, 1, encoded_length, artifact) == encoded_length);
    fclose(artifact);
    assert(airplay_plist_decode(encoded, encoded_length, &decoded, &error));
    assert(strcmp(airplay_plist_get_string(airplay_plist_dict_get(decoded, "name")), expected_name) == 0);
    assert(dict_bool(decoded, "enabled"));
    assert(!dict_bool(decoded, "disabled"));
    assert(dict_uint(decoded, "identifier") == UINT64_C(0xfedcba9876543210));
    assert(airplay_plist_get_data(airplay_plist_dict_get(decoded, "empty"), &empty_length) == NULL);
    assert(empty_length == 0);
    numbers = airplay_plist_dict_get(decoded, "numbers");
    assert(airplay_plist_array_size(numbers) == 20);
    for (index = 0; index < 20; ++index)
    {
        uint64_t value = 0;
        assert(airplay_plist_get_uint(airplay_plist_array_get(numbers, index), &value));
        assert(value == index * index);
    }

    airplay_plist_buffer_free(encoded);
    airplay_plist_free(decoded);
    airplay_plist_free(source);
}

static void test_string_validation(void)
{
    static const char invalid_utf8[] = {(char)0xc0, (char)0x80, '\0'};
    AirPlayPlistValue *dict = airplay_plist_new_dict();
    AirPlayPlistValue *value = airplay_plist_new_bool(true);
    char *maximum_ascii = malloc(AIRPLAY_PLIST_MAX_STRING_BYTES + 1U);
    AirPlayPlistValue *maximum_string;

    assert(dict && value && maximum_ascii);
    assert(airplay_plist_new_string(invalid_utf8) == NULL);
    assert(!airplay_plist_dict_set(dict, invalid_utf8, value));
    airplay_plist_free(value);

    memset(maximum_ascii, 'A', AIRPLAY_PLIST_MAX_STRING_BYTES);
    maximum_ascii[AIRPLAY_PLIST_MAX_STRING_BYTES] = '\0';
    maximum_string = airplay_plist_new_string(maximum_ascii);
    assert(maximum_string != NULL);
    airplay_plist_free(maximum_string);
    free(maximum_ascii);
    airplay_plist_free(dict);
}

static void test_tree_ownership_and_limits(void)
{
    AirPlayPlistValue *first = airplay_plist_new_array();
    AirPlayPlistValue *second = airplay_plist_new_array();
    AirPlayPlistValue *shared = airplay_plist_new_bool(true);
    AirPlayPlistValue *limited = airplay_plist_new_array();
    size_t index;

    assert(first && second && shared && limited);
    assert(airplay_plist_array_append(first, shared));
    assert(!airplay_plist_array_append(second, shared));
    assert(!airplay_plist_array_append(first, first));
    for (index = 0; index < AIRPLAY_PLIST_MAX_CONTAINER_ITEMS; ++index)
        assert(airplay_plist_array_append(limited, airplay_plist_new_bool(false)));
    shared = airplay_plist_new_bool(false);
    assert(shared != NULL);
    assert(!airplay_plist_array_append(limited, shared));
    airplay_plist_free(shared);
    airplay_plist_free(first);
    airplay_plist_free(second);
    airplay_plist_free(limited);
}

static void test_decode_failures(void)
{
    size_t fixture_length;
    uint8_t *fixture = load_hex_fixture("info-response.bplist.hex", &fixture_length);
    AirPlayPlistValue *value = NULL;
    AirPlayPlistError error = AIRPLAY_PLIST_OK;
    uint8_t *oversize_input = calloc(AIRPLAY_PLIST_MAX_BYTES + 1U, 1);
    uint8_t *corrupt = malloc(fixture_length);

    assert(oversize_input != NULL);
    assert(corrupt != NULL);
    assert(!airplay_plist_decode(NULL, fixture_length, &value, &error));
    assert(error == AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT);
    assert(!airplay_plist_decode(fixture, fixture_length, NULL, &error));
    assert(error == AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT);
    assert(!airplay_plist_decode(fixture, fixture_length - 1U, &value, &error));
    assert(error == AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    assert(!airplay_plist_decode(oversize_input, AIRPLAY_PLIST_MAX_BYTES + 1U, &value, &error));
    assert(error == AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);

    memcpy(corrupt, fixture, fixture_length);
    memset(corrupt + fixture_length - 8U, 0xff, 8U);
    assert(!airplay_plist_decode(corrupt, fixture_length, &value, &error));
    assert(error == AIRPLAY_PLIST_ERROR_INVALID_FORMAT);

    free(corrupt);
    free(oversize_input);
    free(fixture);
}

static void test_error_fixtures(void)
{
    struct ErrorFixture
    {
        const char *name;
        AirPlayPlistError expected;
    };
    static const struct ErrorFixture fixtures[] = {
        {"depth-limit.bplist.hex", AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED},
        {"oversize-data.bplist.hex", AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED},
        {"unsupported-object.bplist.hex", AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE}
    };
    size_t index;

    for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index)
    {
        size_t length;
        uint8_t *bytes = load_hex_fixture(fixtures[index].name, &length);
        AirPlayPlistValue *value = NULL;
        AirPlayPlistError error = AIRPLAY_PLIST_OK;
        assert(!airplay_plist_decode(bytes, length, &value, &error));
        assert(value == NULL);
        assert(error == fixtures[index].expected);
        free(bytes);
    }
}

static void test_fixture_mutations_do_not_crash(void)
{
    size_t fixture_length;
    uint8_t *fixture = load_hex_fixture("info-response.bplist.hex", &fixture_length);
    uint8_t *mutation = malloc(fixture_length);
    size_t index;

    assert(mutation != NULL);
    for (index = 0; index < fixture_length; ++index)
    {
        AirPlayPlistValue *value = NULL;
        AirPlayPlistError error = AIRPLAY_PLIST_OK;
        memcpy(mutation, fixture, fixture_length);
        mutation[index] ^= 0x5aU;
        if (airplay_plist_decode(mutation, fixture_length, &value, &error))
            airplay_plist_free(value);
        else
            assert(error != AIRPLAY_PLIST_OK);
    }
    for (index = 0; index < fixture_length; ++index)
    {
        AirPlayPlistValue *value = NULL;
        AirPlayPlistError error = AIRPLAY_PLIST_OK;
        if (airplay_plist_decode(fixture, index, &value, &error))
            airplay_plist_free(value);
        else
            assert(error != AIRPLAY_PLIST_OK);
    }
    free(mutation);
    free(fixture);
}

static void test_encode_depth_limit(void)
{
    AirPlayPlistValue *root = airplay_plist_new_array();
    AirPlayPlistValue *current = root;
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    AirPlayPlistError error = AIRPLAY_PLIST_OK;
    size_t depth;

    assert(root != NULL);
    for (depth = 0; depth < AIRPLAY_PLIST_MAX_DEPTH + 2U; ++depth)
    {
        AirPlayPlistValue *child = airplay_plist_new_array();
        assert(child != NULL);
        assert(airplay_plist_array_append(current, child));
        current = child;
    }
    assert(!airplay_plist_encode(root, &encoded, &encoded_length, &error));
    assert(error == AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
    assert(encoded == NULL);
    assert(encoded_length == 0);
    airplay_plist_free(root);
}

int main(void)
{
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    AirPlayPlistError error = AIRPLAY_PLIST_OK;

    assert(!airplay_plist_encode(NULL, &encoded, &encoded_length, &error));
    assert(error == AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT);
    assert(strcmp(airplay_plist_error_name(AIRPLAY_PLIST_ERROR_INVALID_FORMAT), "invalid-format") == 0);
    assert(strcmp(airplay_plist_error_name((AirPlayPlistError)99), "unknown") == 0);

    test_info_fixture();
    test_round_trip();
    test_string_validation();
    test_tree_ownership_and_limits();
    test_decode_failures();
    test_error_fixtures();
    test_fixture_mutations_do_not_crash();
    test_encode_depth_limit();

    puts("AirPlay binary plist tests passed");
    return 0;
}
