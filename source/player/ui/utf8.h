#ifndef NXCAST_PLAYER_UI_UTF8_H
#define NXCAST_PLAYER_UI_UTF8_H

#include <stddef.h>

// Copies complete UTF-8 code points up to the requested input-byte limit.
size_t player_utf8_copy_prefix(char *output, size_t output_size,
                               const char *input, size_t input_byte_limit);

#endif
