#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AIRPLAY_PLIST_MAX_BYTES (1024U * 1024U)
#define AIRPLAY_PLIST_MAX_DEPTH 32U
#define AIRPLAY_PLIST_MAX_NODES 4096U
#define AIRPLAY_PLIST_MAX_CONTAINER_ITEMS 1024U
#define AIRPLAY_PLIST_MAX_STRING_BYTES (64U * 1024U)
#define AIRPLAY_PLIST_MAX_DATA_BYTES (512U * 1024U)

typedef enum
{
    AIRPLAY_PLIST_TYPE_INVALID = 0,
    AIRPLAY_PLIST_TYPE_BOOL,
    AIRPLAY_PLIST_TYPE_UINT,
    AIRPLAY_PLIST_TYPE_REAL,
    AIRPLAY_PLIST_TYPE_STRING,
    AIRPLAY_PLIST_TYPE_DATA,
    AIRPLAY_PLIST_TYPE_ARRAY,
    AIRPLAY_PLIST_TYPE_DICT
} AirPlayPlistType;

typedef enum
{
    AIRPLAY_PLIST_OK = 0,
    AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT,
    AIRPLAY_PLIST_ERROR_INVALID_FORMAT,
    AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE,
    AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED,
    AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY
} AirPlayPlistError;

typedef struct AirPlayPlistValue AirPlayPlistValue;

AirPlayPlistValue *airplay_plist_new_bool(bool value);
AirPlayPlistValue *airplay_plist_new_uint(uint64_t value);
AirPlayPlistValue *airplay_plist_new_real(double value);
AirPlayPlistValue *airplay_plist_new_string(const char *value);
AirPlayPlistValue *airplay_plist_new_data(const uint8_t *bytes, size_t length);
AirPlayPlistValue *airplay_plist_new_array(void);
AirPlayPlistValue *airplay_plist_new_dict(void);
void airplay_plist_free(AirPlayPlistValue *value);

AirPlayPlistType airplay_plist_type(const AirPlayPlistValue *value);
bool airplay_plist_get_bool(const AirPlayPlistValue *value, bool *value_out);
bool airplay_plist_get_uint(const AirPlayPlistValue *value, uint64_t *value_out);
bool airplay_plist_get_real(const AirPlayPlistValue *value, double *value_out);
const char *airplay_plist_get_string(const AirPlayPlistValue *value);
const uint8_t *airplay_plist_get_data(const AirPlayPlistValue *value, size_t *length_out);

/* Container mutators take ownership of the child only when they return true. */
bool airplay_plist_array_append(AirPlayPlistValue *array, AirPlayPlistValue *item);
size_t airplay_plist_array_size(const AirPlayPlistValue *array);
const AirPlayPlistValue *airplay_plist_array_get(const AirPlayPlistValue *array, size_t index);

bool airplay_plist_dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value);
size_t airplay_plist_dict_size(const AirPlayPlistValue *dict);
const char *airplay_plist_dict_key_at(const AirPlayPlistValue *dict, size_t index);
const AirPlayPlistValue *airplay_plist_dict_value_at(const AirPlayPlistValue *dict, size_t index);
const AirPlayPlistValue *airplay_plist_dict_get(const AirPlayPlistValue *dict, const char *key);

bool airplay_plist_decode(const uint8_t *bytes,
                          size_t length,
                          AirPlayPlistValue **value_out,
                          AirPlayPlistError *error_out);
bool airplay_plist_encode(const AirPlayPlistValue *value,
                          uint8_t **bytes_out,
                          size_t *length_out,
                          AirPlayPlistError *error_out);
void airplay_plist_buffer_free(uint8_t *bytes);
const char *airplay_plist_error_name(AirPlayPlistError error);
