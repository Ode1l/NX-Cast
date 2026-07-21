#pragma once

#include <stdbool.h>
#include <stddef.h>

bool iptv_url_resolve(const char *base, const char *reference,
                      char *output, size_t output_size);
