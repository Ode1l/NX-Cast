#include "player/ui/utf8.h"

#include <stdint.h>
#include <string.h>

static size_t utf8_sequence_length(const unsigned char *input,
                                   size_t available)
{
    uint32_t codepoint;
    size_t length;
    size_t index;

    if (input[0] < 0x80u)
        return 1u;
    if (input[0] >= 0xc2u && input[0] <= 0xdfu)
    {
        codepoint = input[0] & 0x1fu;
        length = 2u;
    }
    else if (input[0] >= 0xe0u && input[0] <= 0xefu)
    {
        codepoint = input[0] & 0x0fu;
        length = 3u;
    }
    else if (input[0] >= 0xf0u && input[0] <= 0xf4u)
    {
        codepoint = input[0] & 0x07u;
        length = 4u;
    }
    else
    {
        return 0u;
    }
    if (available < length)
        return 0u;
    for (index = 1u; index < length; ++index)
    {
        if ((input[index] & 0xc0u) != 0x80u)
            return 0u;
        codepoint = (codepoint << 6u) | (input[index] & 0x3fu);
    }
    if ((length == 3u && codepoint < 0x800u) ||
        (length == 4u && codepoint < 0x10000u) ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
        codepoint > 0x10ffffu)
        return 0u;
    return length;
}

size_t player_utf8_copy_prefix(char *output, size_t output_size,
                               const char *input, size_t input_byte_limit)
{
    size_t input_length;
    size_t input_offset = 0u;
    size_t output_offset = 0u;

    if (!output || output_size == 0u)
        return 0u;
    output[0] = '\0';
    if (!input)
        return 0u;
    input_length = strlen(input);
    while (input_offset < input_length && input_offset < input_byte_limit)
    {
        size_t available = input_length - input_offset;
        size_t sequence_length = utf8_sequence_length(
            (const unsigned char *)input + input_offset, available);

        if (sequence_length == 0u ||
            input_offset + sequence_length > input_byte_limit ||
            output_offset + sequence_length >= output_size)
            break;
        memcpy(output + output_offset, input + input_offset, sequence_length);
        input_offset += sequence_length;
        output_offset += sequence_length;
    }
    output[output_offset] = '\0';
    return output_offset;
}
