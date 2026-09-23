#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

/* Byte budgets, not entry caps. Failure leaves the old allocation intact.
 * Catalog rebuilds keep the previous catalog alive until publication. */
#define IPTV_CHANNEL_BYTES (128U * 1024U * 1024U)
#define IPTV_METADATA_BYTES (8U * 1024U * 1024U)

static inline void *iptv_data_reserve(void *data, size_t *capacity,
                                      size_t needed, size_t element_size,
                                      size_t byte_limit, bool *failed)
{
    if (*failed || needed <= *capacity)
        return data;
    size_t maximum = element_size ? byte_limit / element_size : 0;
    if (maximum > INT_MAX - 3)
        maximum = INT_MAX - 3;
    if (needed > maximum)
    {
        *failed = true;
        return data;
    }
    size_t next = *capacity ? *capacity : (maximum < 16 ? maximum : 16);
    while (next < needed)
        next = next > maximum / 2 ? maximum : next * 2;
    void *replacement = realloc(data, next * element_size);
    if (!replacement)
    {
        *failed = true;
        return data;
    }
    *capacity = next;
    return replacement;
}
