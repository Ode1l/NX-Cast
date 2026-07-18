#pragma once

#include <stdbool.h>
#include <stddef.h>

bool iptv_fetch_to_file(const char *url,
                        const char *destination,
                        size_t maximum_size,
                        char *error,
                        size_t error_size);
