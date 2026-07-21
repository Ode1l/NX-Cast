#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool nxcast_size_add(size_t left, size_t right,
                                   size_t *result_out)
{
    if (!result_out || right > SIZE_MAX - left)
        return false;
    *result_out = left + right;
    return true;
}

static inline bool nxcast_size_multiply(size_t left, size_t right,
                                        size_t *result_out)
{
    if (!result_out || (left != 0u && right > SIZE_MAX / left))
        return false;
    *result_out = left * right;
    return true;
}

static inline bool nxcast_size_grow(size_t current, size_t required,
                                    size_t initial, size_t limit,
                                    size_t *capacity_out)
{
    size_t capacity;

    if (!capacity_out || initial == 0u || current > limit || required > limit)
        return false;
    if (current >= required)
    {
        *capacity_out = current;
        return true;
    }

    capacity = current > 0u ? current : initial;
    if (capacity > limit)
        return false;
    while (capacity < required)
    {
        if (capacity > limit / 2u)
            capacity = limit;
        else
            capacity *= 2u;
        if (capacity < required && capacity == limit)
            return false;
    }

    *capacity_out = capacity;
    return true;
}
