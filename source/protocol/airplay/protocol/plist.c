#include "plist.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *key;
    AirPlayPlistValue *value;
} AirPlayPlistDictEntry;

struct AirPlayPlistValue
{
    AirPlayPlistType type;
    AirPlayPlistValue *parent;
    union
    {
        bool boolean;
        uint64_t integer;
        double real;
        char *string;
        struct
        {
            uint8_t *bytes;
            size_t length;
        } data;
        struct
        {
            AirPlayPlistValue **items;
            size_t length;
        } array;
        struct
        {
            AirPlayPlistDictEntry *entries;
            size_t length;
        } dict;
    } as;
};

typedef struct
{
    uint8_t *bytes;
    size_t length;
    size_t capacity;
    AirPlayPlistError error;
} PlistBuffer;

typedef struct
{
    bool is_key;
    const AirPlayPlistValue *value;
    const char *key;
    size_t *references;
    size_t reference_count;
} PlistEncodeObject;

typedef struct
{
    PlistEncodeObject *objects;
    size_t object_count;
    AirPlayPlistError error;
} PlistEncodeContext;

typedef struct
{
    const uint8_t *bytes;
    size_t length;
    size_t object_table_end;
    size_t offset_table_offset;
    uint8_t offset_size;
    uint8_t reference_size;
    uint64_t object_count;
    uint64_t top_object;
    bool *active_objects;
    size_t decoded_nodes;
    AirPlayPlistError error;
} PlistDecodeContext;

static bool plist_utf8_measure(const char *text, size_t *utf16_units_out, bool *ascii_out);

static void plist_set_error(AirPlayPlistError *error_out, AirPlayPlistError error)
{
    if (error_out)
        *error_out = error;
}

static bool plist_size_add(size_t left, size_t right, size_t *result_out)
{
    if (!result_out || right > SIZE_MAX - left)
        return false;
    *result_out = left + right;
    return true;
}

static bool plist_size_multiply(size_t left, size_t right, size_t *result_out)
{
    if (!result_out || (left != 0 && right > SIZE_MAX / left))
        return false;
    *result_out = left * right;
    return true;
}

static char *plist_string_duplicate(const char *value)
{
    size_t length;
    char *copy;

    if (!value)
        return NULL;
    length = strlen(value);
    if (length > AIRPLAY_PLIST_MAX_STRING_BYTES)
        return NULL;
    copy = malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static AirPlayPlistValue *plist_new_value(AirPlayPlistType type)
{
    AirPlayPlistValue *value = calloc(1, sizeof(*value));

    if (value)
        value->type = type;
    return value;
}

AirPlayPlistValue *airplay_plist_new_bool(bool value)
{
    AirPlayPlistValue *node = plist_new_value(AIRPLAY_PLIST_TYPE_BOOL);

    if (node)
        node->as.boolean = value;
    return node;
}

AirPlayPlistValue *airplay_plist_new_uint(uint64_t value)
{
    AirPlayPlistValue *node = plist_new_value(AIRPLAY_PLIST_TYPE_UINT);

    if (node)
        node->as.integer = value;
    return node;
}

AirPlayPlistValue *airplay_plist_new_real(double value)
{
    AirPlayPlistValue *node = plist_new_value(AIRPLAY_PLIST_TYPE_REAL);

    if (node)
        node->as.real = value;
    return node;
}

AirPlayPlistValue *airplay_plist_new_string(const char *value)
{
    AirPlayPlistValue *node;
    char *copy;
    size_t utf16_units;
    bool ascii;

    if (!plist_utf8_measure(value, &utf16_units, &ascii))
        return NULL;
    (void)utf16_units;
    (void)ascii;
    copy = plist_string_duplicate(value);
    if (!copy)
        return NULL;
    node = plist_new_value(AIRPLAY_PLIST_TYPE_STRING);
    if (!node)
    {
        free(copy);
        return NULL;
    }
    node->as.string = copy;
    return node;
}

AirPlayPlistValue *airplay_plist_new_data(const uint8_t *bytes, size_t length)
{
    AirPlayPlistValue *node;
    uint8_t *copy = NULL;

    if (length > AIRPLAY_PLIST_MAX_DATA_BYTES || (length != 0 && !bytes))
        return NULL;
    if (length != 0)
    {
        copy = malloc(length);
        if (!copy)
            return NULL;
        memcpy(copy, bytes, length);
    }
    node = plist_new_value(AIRPLAY_PLIST_TYPE_DATA);
    if (!node)
    {
        free(copy);
        return NULL;
    }
    node->as.data.bytes = copy;
    node->as.data.length = length;
    return node;
}

AirPlayPlistValue *airplay_plist_new_array(void)
{
    return plist_new_value(AIRPLAY_PLIST_TYPE_ARRAY);
}

AirPlayPlistValue *airplay_plist_new_dict(void)
{
    return plist_new_value(AIRPLAY_PLIST_TYPE_DICT);
}

void airplay_plist_free(AirPlayPlistValue *value)
{
    size_t index;

    if (!value)
        return;
    switch (value->type)
    {
    case AIRPLAY_PLIST_TYPE_STRING:
        free(value->as.string);
        break;
    case AIRPLAY_PLIST_TYPE_DATA:
        free(value->as.data.bytes);
        break;
    case AIRPLAY_PLIST_TYPE_ARRAY:
        for (index = 0; index < value->as.array.length; ++index)
            airplay_plist_free(value->as.array.items[index]);
        free(value->as.array.items);
        break;
    case AIRPLAY_PLIST_TYPE_DICT:
        for (index = 0; index < value->as.dict.length; ++index)
        {
            free(value->as.dict.entries[index].key);
            airplay_plist_free(value->as.dict.entries[index].value);
        }
        free(value->as.dict.entries);
        break;
    default:
        break;
    }
    free(value);
}

AirPlayPlistType airplay_plist_type(const AirPlayPlistValue *value)
{
    return value ? value->type : AIRPLAY_PLIST_TYPE_INVALID;
}

bool airplay_plist_get_bool(const AirPlayPlistValue *value, bool *value_out)
{
    if (!value || !value_out || value->type != AIRPLAY_PLIST_TYPE_BOOL)
        return false;
    *value_out = value->as.boolean;
    return true;
}

bool airplay_plist_get_uint(const AirPlayPlistValue *value, uint64_t *value_out)
{
    if (!value || !value_out || value->type != AIRPLAY_PLIST_TYPE_UINT)
        return false;
    *value_out = value->as.integer;
    return true;
}

bool airplay_plist_get_real(const AirPlayPlistValue *value, double *value_out)
{
    if (!value || !value_out || value->type != AIRPLAY_PLIST_TYPE_REAL)
        return false;
    *value_out = value->as.real;
    return true;
}

const char *airplay_plist_get_string(const AirPlayPlistValue *value)
{
    if (!value || value->type != AIRPLAY_PLIST_TYPE_STRING)
        return NULL;
    return value->as.string;
}

const uint8_t *airplay_plist_get_data(const AirPlayPlistValue *value, size_t *length_out)
{
    if (!value || value->type != AIRPLAY_PLIST_TYPE_DATA)
        return NULL;
    if (length_out)
        *length_out = value->as.data.length;
    return value->as.data.bytes;
}

bool airplay_plist_array_append(AirPlayPlistValue *array, AirPlayPlistValue *item)
{
    AirPlayPlistValue **items;
    size_t allocation_size;

    if (!array || array->type != AIRPLAY_PLIST_TYPE_ARRAY || !item || item->parent || item == array ||
        array->as.array.length >= AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ||
        !plist_size_multiply(array->as.array.length + 1, sizeof(*items), &allocation_size))
    {
        return false;
    }
    items = realloc(array->as.array.items, allocation_size);
    if (!items)
        return false;
    array->as.array.items = items;
    array->as.array.items[array->as.array.length++] = item;
    item->parent = array;
    return true;
}

size_t airplay_plist_array_size(const AirPlayPlistValue *array)
{
    if (!array || array->type != AIRPLAY_PLIST_TYPE_ARRAY)
        return 0;
    return array->as.array.length;
}

const AirPlayPlistValue *airplay_plist_array_get(const AirPlayPlistValue *array, size_t index)
{
    if (!array || array->type != AIRPLAY_PLIST_TYPE_ARRAY || index >= array->as.array.length)
        return NULL;
    return array->as.array.items[index];
}

static size_t plist_dict_find(const AirPlayPlistValue *dict, const char *key)
{
    size_t index;

    if (!dict || dict->type != AIRPLAY_PLIST_TYPE_DICT || !key)
        return SIZE_MAX;
    for (index = 0; index < dict->as.dict.length; ++index)
    {
        if (strcmp(dict->as.dict.entries[index].key, key) == 0)
            return index;
    }
    return SIZE_MAX;
}

bool airplay_plist_dict_set(AirPlayPlistValue *dict, const char *key, AirPlayPlistValue *value)
{
    AirPlayPlistDictEntry *entries;
    size_t index;
    size_t allocation_size;
    size_t utf16_units;
    bool ascii;
    char *key_copy;

    if (!dict || dict->type != AIRPLAY_PLIST_TYPE_DICT || !key || !value || value->parent || value == dict ||
        !plist_utf8_measure(key, &utf16_units, &ascii))
    {
        return false;
    }
    (void)utf16_units;
    (void)ascii;
    key_copy = plist_string_duplicate(key);
    if (!key_copy)
        return false;

    index = plist_dict_find(dict, key);
    if (index != SIZE_MAX)
    {
        free(key_copy);
        airplay_plist_free(dict->as.dict.entries[index].value);
        dict->as.dict.entries[index].value = value;
        value->parent = dict;
        return true;
    }

    if (dict->as.dict.length >= AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ||
        !plist_size_multiply(dict->as.dict.length + 1, sizeof(*entries), &allocation_size))
    {
        free(key_copy);
        return false;
    }
    entries = realloc(dict->as.dict.entries, allocation_size);
    if (!entries)
    {
        free(key_copy);
        return false;
    }
    dict->as.dict.entries = entries;
    dict->as.dict.entries[dict->as.dict.length].key = key_copy;
    dict->as.dict.entries[dict->as.dict.length].value = value;
    dict->as.dict.length++;
    value->parent = dict;
    return true;
}

size_t airplay_plist_dict_size(const AirPlayPlistValue *dict)
{
    if (!dict || dict->type != AIRPLAY_PLIST_TYPE_DICT)
        return 0;
    return dict->as.dict.length;
}

const char *airplay_plist_dict_key_at(const AirPlayPlistValue *dict, size_t index)
{
    if (!dict || dict->type != AIRPLAY_PLIST_TYPE_DICT || index >= dict->as.dict.length)
        return NULL;
    return dict->as.dict.entries[index].key;
}

const AirPlayPlistValue *airplay_plist_dict_value_at(const AirPlayPlistValue *dict, size_t index)
{
    if (!dict || dict->type != AIRPLAY_PLIST_TYPE_DICT || index >= dict->as.dict.length)
        return NULL;
    return dict->as.dict.entries[index].value;
}

const AirPlayPlistValue *airplay_plist_dict_get(const AirPlayPlistValue *dict, const char *key)
{
    size_t index = plist_dict_find(dict, key);

    if (index == SIZE_MAX)
        return NULL;
    return dict->as.dict.entries[index].value;
}

static bool plist_utf8_next(const char *text, size_t length, size_t *index, uint32_t *codepoint_out)
{
    size_t position;
    uint8_t first;
    uint32_t codepoint;
    size_t continuation_count;
    size_t offset;

    if (!text || !index || !codepoint_out || *index >= length)
        return false;
    position = *index;
    first = (uint8_t)text[position];
    if (first < 0x80)
    {
        if (first == 0)
            return false;
        *codepoint_out = first;
        *index = position + 1;
        return true;
    }
    if ((first & 0xe0) == 0xc0)
    {
        codepoint = first & 0x1f;
        continuation_count = 1;
        if (codepoint < 2)
            return false;
    }
    else if ((first & 0xf0) == 0xe0)
    {
        codepoint = first & 0x0f;
        continuation_count = 2;
    }
    else if ((first & 0xf8) == 0xf0)
    {
        codepoint = first & 0x07;
        continuation_count = 3;
        if (codepoint > 4)
            return false;
    }
    else
        return false;

    if (continuation_count > length - position - 1)
        return false;
    for (offset = 1; offset <= continuation_count; ++offset)
    {
        uint8_t next = (uint8_t)text[position + offset];
        if ((next & 0xc0) != 0x80)
            return false;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((continuation_count == 2 && codepoint < 0x800) ||
        (continuation_count == 3 && codepoint < 0x10000) ||
        codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
    {
        return false;
    }
    *codepoint_out = codepoint;
    *index = position + continuation_count + 1;
    return true;
}

static bool plist_utf8_measure(const char *text, size_t *utf16_units_out, bool *ascii_out)
{
    size_t length;
    size_t index = 0;
    size_t units = 0;
    bool ascii = true;

    if (!text || !utf16_units_out || !ascii_out)
        return false;
    length = strlen(text);
    if (length > AIRPLAY_PLIST_MAX_STRING_BYTES)
        return false;
    while (index < length)
    {
        uint32_t codepoint;
        if (!plist_utf8_next(text, length, &index, &codepoint))
            return false;
        if (codepoint >= 0x80)
            ascii = false;
        units += codepoint >= 0x10000 ? 2U : 1U;
        if (units > AIRPLAY_PLIST_MAX_STRING_BYTES)
            return false;
    }
    if (!ascii && units > AIRPLAY_PLIST_MAX_STRING_BYTES / 2U)
        return false;
    *utf16_units_out = units;
    *ascii_out = ascii;
    return true;
}

static bool plist_buffer_reserve(PlistBuffer *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    uint8_t *bytes;

    if (!buffer || !plist_size_add(buffer->length, additional, &required) || required > AIRPLAY_PLIST_MAX_BYTES)
    {
        if (buffer)
            buffer->error = AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED;
        return false;
    }
    if (required <= buffer->capacity)
        return true;
    capacity = buffer->capacity ? buffer->capacity : 128U;
    while (capacity < required)
    {
        if (capacity > AIRPLAY_PLIST_MAX_BYTES / 2U)
        {
            capacity = AIRPLAY_PLIST_MAX_BYTES;
            break;
        }
        capacity *= 2U;
    }
    bytes = realloc(buffer->bytes, capacity);
    if (!bytes)
    {
        buffer->error = AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY;
        return false;
    }
    buffer->bytes = bytes;
    buffer->capacity = capacity;
    return true;
}

static bool plist_buffer_write(PlistBuffer *buffer, const void *bytes, size_t length)
{
    if (!buffer || (length != 0 && !bytes) || !plist_buffer_reserve(buffer, length))
        return false;
    if (length != 0)
        memcpy(buffer->bytes + buffer->length, bytes, length);
    buffer->length += length;
    return true;
}

static bool plist_buffer_write_byte(PlistBuffer *buffer, uint8_t value)
{
    return plist_buffer_write(buffer, &value, 1);
}

static bool plist_buffer_write_uint(PlistBuffer *buffer, uint64_t value, uint8_t width)
{
    uint8_t bytes[8];
    uint8_t index;

    if (width == 0 || width > sizeof(bytes))
        return false;
    for (index = 0; index < width; ++index)
        bytes[width - index - 1] = (uint8_t)(value >> (index * 8U));
    return plist_buffer_write(buffer, bytes, width);
}

static uint8_t plist_uint_width(uint64_t value)
{
    if (value <= UINT8_MAX)
        return 1;
    if (value <= UINT16_MAX)
        return 2;
    if (value <= UINT32_MAX)
        return 4;
    return 8;
}

static uint8_t plist_width_exponent(uint8_t width)
{
    switch (width)
    {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    default:
        return 3;
    }
}

static bool plist_buffer_write_marker_length(PlistBuffer *buffer, uint8_t marker, size_t length)
{
    uint8_t width;

    if (length < 15U)
        return plist_buffer_write_byte(buffer, (uint8_t)(marker | (uint8_t)length));
    width = plist_uint_width((uint64_t)length);
    return plist_buffer_write_byte(buffer, (uint8_t)(marker | 0x0fU)) &&
           plist_buffer_write_byte(buffer, (uint8_t)(0x10U | plist_width_exponent(width))) &&
           plist_buffer_write_uint(buffer, (uint64_t)length, width);
}

static bool plist_encode_string(PlistBuffer *buffer, const char *text)
{
    size_t length;
    size_t utf16_units;
    size_t index = 0;
    bool ascii;

    if (!text || !plist_utf8_measure(text, &utf16_units, &ascii))
    {
        if (buffer)
            buffer->error = AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT;
        return false;
    }
    length = strlen(text);
    if (ascii)
        return plist_buffer_write_marker_length(buffer, 0x50U, length) &&
               plist_buffer_write(buffer, text, length);

    if (!plist_buffer_write_marker_length(buffer, 0x60U, utf16_units))
        return false;
    while (index < length)
    {
        uint32_t codepoint;
        if (!plist_utf8_next(text, length, &index, &codepoint))
        {
            buffer->error = AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT;
            return false;
        }
        if (codepoint < 0x10000)
        {
            if (!plist_buffer_write_uint(buffer, codepoint, 2))
                return false;
        }
        else
        {
            uint32_t adjusted = codepoint - 0x10000;
            uint16_t high = (uint16_t)(0xd800U | (adjusted >> 10));
            uint16_t low = (uint16_t)(0xdc00U | (adjusted & 0x3ffU));
            if (!plist_buffer_write_uint(buffer, high, 2) || !plist_buffer_write_uint(buffer, low, 2))
                return false;
        }
    }
    return true;
}

static bool plist_encode_collect_value(PlistEncodeContext *context,
                                       const AirPlayPlistValue *value,
                                       size_t depth,
                                       size_t *object_id_out);

static bool plist_encode_add_key(PlistEncodeContext *context, const char *key, size_t *object_id_out)
{
    size_t utf16_units;
    bool ascii;
    PlistEncodeObject *object;

    if (!context || !key || !object_id_out || !plist_utf8_measure(key, &utf16_units, &ascii))
    {
        if (context)
            context->error = AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT;
        return false;
    }
    (void)utf16_units;
    (void)ascii;
    if (context->object_count >= AIRPLAY_PLIST_MAX_NODES)
    {
        context->error = AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED;
        return false;
    }
    *object_id_out = context->object_count;
    object = &context->objects[context->object_count++];
    object->is_key = true;
    object->key = key;
    return true;
}

static bool plist_encode_collect_value(PlistEncodeContext *context,
                                       const AirPlayPlistValue *value,
                                       size_t depth,
                                       size_t *object_id_out)
{
    PlistEncodeObject *object;
    size_t index;
    size_t reference_count = 0;

    if (!context || !value || !object_id_out)
        return false;
    if (depth > AIRPLAY_PLIST_MAX_DEPTH || context->object_count >= AIRPLAY_PLIST_MAX_NODES)
    {
        context->error = AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED;
        return false;
    }
    if (value->type < AIRPLAY_PLIST_TYPE_BOOL || value->type > AIRPLAY_PLIST_TYPE_DICT)
    {
        context->error = AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE;
        return false;
    }
    *object_id_out = context->object_count;
    object = &context->objects[context->object_count++];
    object->value = value;

    if (value->type == AIRPLAY_PLIST_TYPE_ARRAY)
        reference_count = value->as.array.length;
    else if (value->type == AIRPLAY_PLIST_TYPE_DICT &&
             !plist_size_multiply(value->as.dict.length, 2U, &reference_count))
    {
        context->error = AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED;
        return false;
    }
    if (reference_count == 0)
        return true;
    if (reference_count > AIRPLAY_PLIST_MAX_CONTAINER_ITEMS * 2U)
    {
        context->error = AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED;
        return false;
    }
    object->references = calloc(reference_count, sizeof(*object->references));
    if (!object->references)
    {
        context->error = AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY;
        return false;
    }
    object->reference_count = reference_count;

    if (value->type == AIRPLAY_PLIST_TYPE_ARRAY)
    {
        for (index = 0; index < value->as.array.length; ++index)
        {
            if (!plist_encode_collect_value(context,
                                            value->as.array.items[index],
                                            depth + 1U,
                                            &object->references[index]))
            {
                return false;
            }
        }
    }
    else
    {
        for (index = 0; index < value->as.dict.length; ++index)
        {
            if (!plist_encode_add_key(context, value->as.dict.entries[index].key, &object->references[index]))
                return false;
        }
        for (index = 0; index < value->as.dict.length; ++index)
        {
            if (!plist_encode_collect_value(context,
                                            value->as.dict.entries[index].value,
                                            depth + 1U,
                                            &object->references[value->as.dict.length + index]))
            {
                return false;
            }
        }
    }
    return true;
}

static bool plist_encode_object(PlistBuffer *buffer,
                                const PlistEncodeObject *object,
                                uint8_t reference_size)
{
    const AirPlayPlistValue *value;
    size_t index;

    if (!buffer || !object)
        return false;
    if (object->is_key)
        return plist_encode_string(buffer, object->key);
    value = object->value;
    switch (value->type)
    {
    case AIRPLAY_PLIST_TYPE_BOOL:
        return plist_buffer_write_byte(buffer, value->as.boolean ? 0x09U : 0x08U);
    case AIRPLAY_PLIST_TYPE_UINT:
    {
        uint8_t width = plist_uint_width(value->as.integer);
        return plist_buffer_write_byte(buffer, (uint8_t)(0x10U | plist_width_exponent(width))) &&
               plist_buffer_write_uint(buffer, value->as.integer, width);
    }
    case AIRPLAY_PLIST_TYPE_REAL:
    {
        uint64_t bits;
        memcpy(&bits, &value->as.real, sizeof(bits));
        return plist_buffer_write_byte(buffer, 0x23U) && plist_buffer_write_uint(buffer, bits, 8);
    }
    case AIRPLAY_PLIST_TYPE_STRING:
        return plist_encode_string(buffer, value->as.string);
    case AIRPLAY_PLIST_TYPE_DATA:
        return plist_buffer_write_marker_length(buffer, 0x40U, value->as.data.length) &&
               plist_buffer_write(buffer, value->as.data.bytes, value->as.data.length);
    case AIRPLAY_PLIST_TYPE_ARRAY:
        if (!plist_buffer_write_marker_length(buffer, 0xa0U, value->as.array.length))
            return false;
        for (index = 0; index < object->reference_count; ++index)
        {
            if (!plist_buffer_write_uint(buffer, object->references[index], reference_size))
                return false;
        }
        return true;
    case AIRPLAY_PLIST_TYPE_DICT:
        if (!plist_buffer_write_marker_length(buffer, 0xd0U, value->as.dict.length))
            return false;
        for (index = 0; index < object->reference_count; ++index)
        {
            if (!plist_buffer_write_uint(buffer, object->references[index], reference_size))
                return false;
        }
        return true;
    default:
        buffer->error = AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE;
        return false;
    }
}

static void plist_write_uint_to_bytes(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index)
        bytes[7U - index] = (uint8_t)(value >> (index * 8U));
}

bool airplay_plist_encode(const AirPlayPlistValue *value,
                          uint8_t **bytes_out,
                          size_t *length_out,
                          AirPlayPlistError *error_out)
{
    static const uint8_t header[] = {'b', 'p', 'l', 'i', 's', 't', '0', '0'};
    PlistEncodeContext context = {0};
    PlistBuffer buffer = {0};
    size_t *offsets = NULL;
    size_t root_object = 0;
    size_t index;
    size_t offset_table_offset;
    uint8_t reference_size;
    uint8_t offset_size;
    uint8_t trailer[32] = {0};
    bool success = false;

    if (!value || !bytes_out || !length_out)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT);
        return false;
    }
    *bytes_out = NULL;
    *length_out = 0;
    plist_set_error(error_out, AIRPLAY_PLIST_OK);

    context.objects = calloc(AIRPLAY_PLIST_MAX_NODES, sizeof(*context.objects));
    if (!context.objects)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
        return false;
    }
    if (!plist_encode_collect_value(&context, value, 0, &root_object))
        goto cleanup;
    offsets = calloc(context.object_count, sizeof(*offsets));
    if (!offsets)
    {
        context.error = AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    reference_size = plist_uint_width(context.object_count - 1U);
    if (!plist_buffer_write(&buffer, header, sizeof(header)))
        goto cleanup;
    for (index = 0; index < context.object_count; ++index)
    {
        offsets[index] = buffer.length;
        if (!plist_encode_object(&buffer, &context.objects[index], reference_size))
            goto cleanup;
    }
    offset_table_offset = buffer.length;
    offset_size = plist_uint_width((uint64_t)offset_table_offset);
    for (index = 0; index < context.object_count; ++index)
    {
        if (!plist_buffer_write_uint(&buffer, offsets[index], offset_size))
            goto cleanup;
    }
    trailer[6] = offset_size;
    trailer[7] = reference_size;
    plist_write_uint_to_bytes(trailer + 8, context.object_count);
    plist_write_uint_to_bytes(trailer + 16, root_object);
    plist_write_uint_to_bytes(trailer + 24, offset_table_offset);
    if (!plist_buffer_write(&buffer, trailer, sizeof(trailer)))
        goto cleanup;

    *bytes_out = buffer.bytes;
    *length_out = buffer.length;
    buffer.bytes = NULL;
    success = true;

cleanup:
    if (!success)
    {
        AirPlayPlistError error = context.error != AIRPLAY_PLIST_OK ? context.error : buffer.error;
        if (error == AIRPLAY_PLIST_OK)
            error = AIRPLAY_PLIST_ERROR_INVALID_FORMAT;
        plist_set_error(error_out, error);
    }
    for (index = 0; index < context.object_count; ++index)
        free(context.objects[index].references);
    free(context.objects);
    free(offsets);
    free(buffer.bytes);
    return success;
}

static bool plist_decode_fail(PlistDecodeContext *context, AirPlayPlistError error)
{
    if (context && context->error == AIRPLAY_PLIST_OK)
        context->error = error;
    return false;
}

static bool plist_read_uint(const uint8_t *bytes,
                            size_t available,
                            uint8_t width,
                            uint64_t *value_out)
{
    uint64_t value = 0;
    uint8_t index;

    if (!bytes || !value_out || width == 0 || width > 8 || width > available)
        return false;
    for (index = 0; index < width; ++index)
        value = (value << 8) | bytes[index];
    *value_out = value;
    return true;
}

static bool plist_decode_offset(const PlistDecodeContext *context,
                                uint64_t object_id,
                                size_t *offset_out)
{
    size_t table_index;
    size_t byte_offset;
    uint64_t offset;

    if (!context || !offset_out || object_id >= context->object_count ||
        object_id > SIZE_MAX ||
        !plist_size_multiply((size_t)object_id, context->offset_size, &table_index) ||
        !plist_size_add(context->offset_table_offset, table_index, &byte_offset) ||
        !plist_read_uint(context->bytes + byte_offset,
                         context->length - byte_offset,
                         context->offset_size,
                         &offset) ||
        offset < 8U || offset >= context->object_table_end || offset > SIZE_MAX)
    {
        return false;
    }
    *offset_out = (size_t)offset;
    return true;
}

static bool plist_decode_length(PlistDecodeContext *context,
                                size_t object_offset,
                                uint8_t marker,
                                size_t *length_out,
                                size_t *header_size_out)
{
    uint8_t low = marker & 0x0fU;
    size_t integer_offset;
    uint8_t integer_marker;
    uint8_t exponent;
    uint8_t width;
    uint64_t length;
    size_t header_size;

    if (low < 0x0fU)
    {
        *length_out = low;
        *header_size_out = 1;
        return true;
    }
    if (!plist_size_add(object_offset, 1U, &integer_offset) || integer_offset >= context->object_table_end)
        return plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    integer_marker = context->bytes[integer_offset];
    if ((integer_marker & 0xf0U) != 0x10U)
        return plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    exponent = integer_marker & 0x0fU;
    if (exponent > 3U)
        return plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
    width = (uint8_t)(1U << exponent);
    if (!plist_size_add(2U, width, &header_size) || header_size > context->object_table_end - object_offset ||
        !plist_read_uint(context->bytes + integer_offset + 1U,
                         context->object_table_end - integer_offset - 1U,
                         width,
                         &length) ||
        length > SIZE_MAX)
    {
        return plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    }
    *length_out = (size_t)length;
    *header_size_out = header_size;
    return true;
}

static bool plist_utf8_append(uint8_t *output,
                              size_t output_size,
                              size_t *used,
                              uint32_t codepoint)
{
    size_t required;

    if (codepoint == 0 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return false;
    required = codepoint < 0x80 ? 1U : codepoint < 0x800 ? 2U : codepoint < 0x10000 ? 3U : 4U;
    if (*used > output_size || required > output_size - *used)
        return false;
    if (required == 1U)
        output[(*used)++] = (uint8_t)codepoint;
    else if (required == 2U)
    {
        output[(*used)++] = (uint8_t)(0xc0U | (codepoint >> 6));
        output[(*used)++] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    }
    else if (required == 3U)
    {
        output[(*used)++] = (uint8_t)(0xe0U | (codepoint >> 12));
        output[(*used)++] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        output[(*used)++] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    }
    else
    {
        output[(*used)++] = (uint8_t)(0xf0U | (codepoint >> 18));
        output[(*used)++] = (uint8_t)(0x80U | ((codepoint >> 12) & 0x3fU));
        output[(*used)++] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        output[(*used)++] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    }
    return true;
}

static AirPlayPlistValue *plist_decode_ascii_string(PlistDecodeContext *context,
                                                    const uint8_t *bytes,
                                                    size_t length)
{
    AirPlayPlistValue *value;
    char *text;
    size_t index;

    if (length > AIRPLAY_PLIST_MAX_STRING_BYTES)
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
        return NULL;
    }
    text = malloc(length + 1U);
    if (!text)
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    for (index = 0; index < length; ++index)
    {
        if (bytes[index] == 0 || bytes[index] >= 0x80)
        {
            free(text);
            plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            return NULL;
        }
        text[index] = (char)bytes[index];
    }
    text[length] = '\0';
    value = airplay_plist_new_string(text);
    free(text);
    if (!value)
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
    return value;
}

static AirPlayPlistValue *plist_decode_utf16_string(PlistDecodeContext *context,
                                                    const uint8_t *bytes,
                                                    size_t unit_count)
{
    size_t maximum_bytes;
    size_t used = 0;
    size_t index = 0;
    uint8_t *text;
    AirPlayPlistValue *value;

    if (unit_count > AIRPLAY_PLIST_MAX_STRING_BYTES / 2U ||
        !plist_size_multiply(unit_count, 3U, &maximum_bytes))
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
        return NULL;
    }
    if (maximum_bytes > AIRPLAY_PLIST_MAX_STRING_BYTES)
        maximum_bytes = AIRPLAY_PLIST_MAX_STRING_BYTES;
    text = malloc(maximum_bytes + 1U);
    if (!text)
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
        return NULL;
    }
    while (index < unit_count)
    {
        uint16_t first = (uint16_t)(((uint16_t)bytes[index * 2U] << 8) | bytes[index * 2U + 1U]);
        uint32_t codepoint = first;
        index++;
        if (first >= 0xd800U && first <= 0xdbffU)
        {
            uint16_t second;
            if (index >= unit_count)
                goto invalid;
            second = (uint16_t)(((uint16_t)bytes[index * 2U] << 8) | bytes[index * 2U + 1U]);
            if (second < 0xdc00U || second > 0xdfffU)
                goto invalid;
            index++;
            codepoint = 0x10000U + (((uint32_t)first - 0xd800U) << 10) + ((uint32_t)second - 0xdc00U);
        }
        else if (first >= 0xdc00U && first <= 0xdfffU)
            goto invalid;
        if (codepoint == 0)
            goto invalid;
        if (!plist_utf8_append(text, maximum_bytes, &used, codepoint))
        {
            free(text);
            plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
            return NULL;
        }
    }
    text[used] = '\0';
    value = airplay_plist_new_string((const char *)text);
    free(text);
    if (!value)
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
    return value;

invalid:
    free(text);
    plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    return NULL;
}

static bool plist_decode_reference(PlistDecodeContext *context, size_t offset, uint64_t *reference_out)
{
    if (offset >= context->object_table_end ||
        !plist_read_uint(context->bytes + offset,
                         context->object_table_end - offset,
                         context->reference_size,
                         reference_out) ||
        *reference_out >= context->object_count)
    {
        return plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
    }
    return true;
}

static AirPlayPlistValue *plist_decode_object(PlistDecodeContext *context, uint64_t object_id, size_t depth)
{
    size_t object_offset;
    size_t item_length;
    size_t header_size;
    size_t payload_offset;
    size_t payload_bytes;
    uint8_t marker;
    uint8_t high;
    uint8_t low;
    AirPlayPlistValue *value = NULL;

    if (depth > AIRPLAY_PLIST_MAX_DEPTH || context->decoded_nodes >= AIRPLAY_PLIST_MAX_NODES)
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
        return NULL;
    }
    if (object_id >= context->object_count || context->active_objects[object_id] ||
        !plist_decode_offset(context, object_id, &object_offset))
    {
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        return NULL;
    }
    context->active_objects[object_id] = true;
    context->decoded_nodes++;
    marker = context->bytes[object_offset];
    high = marker & 0xf0U;
    low = marker & 0x0fU;

    if (marker == 0x08U || marker == 0x09U)
        value = airplay_plist_new_bool(marker == 0x09U);
    else if (high == 0x10U)
    {
        uint8_t width;
        uint64_t integer;
        if (low > 3U)
            plist_decode_fail(context, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
        else
        {
            width = (uint8_t)(1U << low);
            if (width > context->object_table_end - object_offset - 1U ||
                !plist_read_uint(context->bytes + object_offset + 1U,
                                 context->object_table_end - object_offset - 1U,
                                 width,
                                 &integer))
            {
                plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            }
            else
                value = airplay_plist_new_uint(integer);
        }
    }
    else if (high == 0x20U)
    {
        uint8_t width = low <= 3U ? (uint8_t)(1U << low) : 0;
        uint64_t bits;
        if ((width != 4U && width != 8U) || width > context->object_table_end - object_offset - 1U ||
            !plist_read_uint(context->bytes + object_offset + 1U,
                             context->object_table_end - object_offset - 1U,
                             width,
                             &bits))
        {
            plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        }
        else if (width == 4U)
        {
            uint32_t bits32 = (uint32_t)bits;
            float real32;
            memcpy(&real32, &bits32, sizeof(real32));
            value = airplay_plist_new_real(real32);
        }
        else
        {
            double real64;
            memcpy(&real64, &bits, sizeof(real64));
            value = airplay_plist_new_real(real64);
        }
    }
    else if (high == 0x40U || high == 0x50U || high == 0x60U || high == 0xa0U || high == 0xd0U)
    {
        if (!plist_decode_length(context, object_offset, marker, &item_length, &header_size) ||
            !plist_size_add(object_offset, header_size, &payload_offset))
        {
            goto done;
        }
        if (high == 0x40U)
        {
            if (item_length > AIRPLAY_PLIST_MAX_DATA_BYTES || item_length > context->object_table_end - payload_offset)
                plist_decode_fail(context, item_length > AIRPLAY_PLIST_MAX_DATA_BYTES ? AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED : AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            else
                value = airplay_plist_new_data(context->bytes + payload_offset, item_length);
        }
        else if (high == 0x50U)
        {
            if (item_length > context->object_table_end - payload_offset)
                plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            else
                value = plist_decode_ascii_string(context, context->bytes + payload_offset, item_length);
        }
        else if (high == 0x60U)
        {
            if (!plist_size_multiply(item_length, 2U, &payload_bytes) ||
                payload_bytes > context->object_table_end - payload_offset)
            {
                plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            }
            else
                value = plist_decode_utf16_string(context, context->bytes + payload_offset, item_length);
        }
        else if (high == 0xa0U)
        {
            size_t index;
            if (item_length > AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ||
                !plist_size_multiply(item_length, context->reference_size, &payload_bytes) ||
                payload_bytes > context->object_table_end - payload_offset)
            {
                plist_decode_fail(context, item_length > AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ? AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED : AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
                goto done;
            }
            value = airplay_plist_new_array();
            if (!value)
            {
                plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
                goto done;
            }
            for (index = 0; index < item_length; ++index)
            {
                uint64_t reference;
                AirPlayPlistValue *item = NULL;
                if (!plist_decode_reference(context,
                                            payload_offset + index * context->reference_size,
                                            &reference) ||
                    !(item = plist_decode_object(context, reference, depth + 1U)) ||
                    !airplay_plist_array_append(value, item))
                {
                    if (context->error == AIRPLAY_PLIST_OK)
                        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
                    if (item && !item->parent)
                        airplay_plist_free(item);
                    airplay_plist_free(value);
                    value = NULL;
                    goto done;
                }
            }
        }
        else
        {
            size_t index;
            size_t references_bytes;
            if (item_length > AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ||
                !plist_size_multiply(item_length, context->reference_size, &references_bytes) ||
                !plist_size_multiply(references_bytes, 2U, &payload_bytes) ||
                payload_bytes > context->object_table_end - payload_offset)
            {
                plist_decode_fail(context, item_length > AIRPLAY_PLIST_MAX_CONTAINER_ITEMS ? AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED : AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
                goto done;
            }
            value = airplay_plist_new_dict();
            if (!value)
            {
                plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
                goto done;
            }
            for (index = 0; index < item_length; ++index)
            {
                uint64_t key_reference;
                uint64_t value_reference;
                AirPlayPlistValue *key_node = NULL;
                AirPlayPlistValue *item = NULL;
                const char *key = NULL;
                bool item_added = false;

                if (plist_decode_reference(context,
                                           payload_offset + index * context->reference_size,
                                           &key_reference) &&
                    plist_decode_reference(context,
                                           payload_offset + references_bytes + index * context->reference_size,
                                           &value_reference))
                {
                    key_node = plist_decode_object(context, key_reference, depth + 1U);
                    if (key_node)
                        key = airplay_plist_get_string(key_node);
                    if (key_node && (!key || airplay_plist_dict_get(value, key)))
                        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);

                    if (key && context->error == AIRPLAY_PLIST_OK)
                        item = plist_decode_object(context, value_reference, depth + 1U);
                    if (item && context->error == AIRPLAY_PLIST_OK)
                    {
                        item_added = airplay_plist_dict_set(value, key, item);
                        if (!item_added)
                            plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
                    }
                }

                if (!item_added)
                {
                    if (item && !item->parent)
                        airplay_plist_free(item);
                    airplay_plist_free(key_node);
                    airplay_plist_free(value);
                    value = NULL;
                    goto done;
                }
                airplay_plist_free(key_node);
            }
        }
    }
    else
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE);

    if (!value && context->error == AIRPLAY_PLIST_OK)
        plist_decode_fail(context, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);

done:
    context->active_objects[object_id] = false;
    return value;
}

bool airplay_plist_decode(const uint8_t *bytes,
                          size_t length,
                          AirPlayPlistValue **value_out,
                          AirPlayPlistError *error_out)
{
    PlistDecodeContext context = {0};
    const uint8_t *trailer;
    uint64_t offset_table_bytes;
    uint64_t offset_table_end;
    uint64_t index;
    size_t trailer_offset;
    AirPlayPlistValue *value = NULL;

    if (!bytes || !value_out)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT);
        return false;
    }
    *value_out = NULL;
    plist_set_error(error_out, AIRPLAY_PLIST_OK);
    if (length > AIRPLAY_PLIST_MAX_BYTES)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED);
        return false;
    }
    if (length < 40U || memcmp(bytes, "bplist00", 8) != 0)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        return false;
    }

    trailer = bytes + length - 32U;
    trailer_offset = length - 32U;
    for (index = 0; index < 6U; ++index)
    {
        if (trailer[index] != 0)
        {
            plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            return false;
        }
    }
    context.bytes = bytes;
    context.length = length;
    context.object_table_end = trailer_offset;
    context.offset_size = trailer[6];
    context.reference_size = trailer[7];
    if (!plist_read_uint(trailer + 8, 24, 8, &context.object_count) ||
        !plist_read_uint(trailer + 16, 16, 8, &context.top_object) ||
        !plist_read_uint(trailer + 24, 8, 8, &offset_table_bytes))
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        return false;
    }
    context.offset_table_offset = (size_t)offset_table_bytes;
    if ((context.offset_size != 1U && context.offset_size != 2U && context.offset_size != 4U && context.offset_size != 8U) ||
        (context.reference_size != 1U && context.reference_size != 2U && context.reference_size != 4U && context.reference_size != 8U) ||
        context.object_count == 0 || context.object_count > AIRPLAY_PLIST_MAX_NODES ||
        context.top_object >= context.object_count || offset_table_bytes > SIZE_MAX ||
        context.offset_table_offset < 8U || context.offset_table_offset >= context.object_table_end ||
        context.object_count > UINT64_MAX / context.offset_size)
    {
        plist_set_error(error_out, context.object_count > AIRPLAY_PLIST_MAX_NODES ? AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED : AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        return false;
    }
    offset_table_bytes = context.object_count * context.offset_size;
    offset_table_end = (uint64_t)context.offset_table_offset + offset_table_bytes;
    if (offset_table_end > trailer_offset)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
        return false;
    }
    context.object_table_end = context.offset_table_offset;
    for (index = 0; index < context.object_count; ++index)
    {
        size_t offset;
        if (!plist_decode_offset(&context, index, &offset))
        {
            plist_set_error(error_out, AIRPLAY_PLIST_ERROR_INVALID_FORMAT);
            return false;
        }
    }
    context.active_objects = calloc((size_t)context.object_count, sizeof(*context.active_objects));
    if (!context.active_objects)
    {
        plist_set_error(error_out, AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY);
        return false;
    }
    value = plist_decode_object(&context, context.top_object, 0);
    free(context.active_objects);
    if (!value)
    {
        plist_set_error(error_out,
                        context.error == AIRPLAY_PLIST_OK ? AIRPLAY_PLIST_ERROR_INVALID_FORMAT : context.error);
        return false;
    }
    *value_out = value;
    return true;
}

void airplay_plist_buffer_free(uint8_t *bytes)
{
    free(bytes);
}

const char *airplay_plist_error_name(AirPlayPlistError error)
{
    switch (error)
    {
    case AIRPLAY_PLIST_OK:
        return "ok";
    case AIRPLAY_PLIST_ERROR_INVALID_ARGUMENT:
        return "invalid-argument";
    case AIRPLAY_PLIST_ERROR_INVALID_FORMAT:
        return "invalid-format";
    case AIRPLAY_PLIST_ERROR_UNSUPPORTED_TYPE:
        return "unsupported-type";
    case AIRPLAY_PLIST_ERROR_LIMIT_EXCEEDED:
        return "limit-exceeded";
    case AIRPLAY_PLIST_ERROR_OUT_OF_MEMORY:
        return "out-of-memory";
    default:
        return "unknown";
    }
}
