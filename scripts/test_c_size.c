#include "util/size.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    size_t value = 0u;

    assert(nxcast_size_add(4u, 5u, &value) && value == 9u);
    assert(!nxcast_size_add(SIZE_MAX, 1u, &value));
    assert(!nxcast_size_add(1u, 1u, NULL));

    assert(nxcast_size_multiply(6u, 7u, &value) && value == 42u);
    assert(nxcast_size_multiply(0u, SIZE_MAX, &value) && value == 0u);
    assert(!nxcast_size_multiply(SIZE_MAX, 2u, &value));

    assert(nxcast_size_grow(0u, 1u, 8u, 64u, &value) && value == 8u);
    assert(nxcast_size_grow(8u, 9u, 8u, 64u, &value) && value == 16u);
    assert(nxcast_size_grow(16u, 63u, 8u, 64u, &value) && value == 64u);
    assert(!nxcast_size_grow(16u, 65u, 8u, 64u, &value));
    assert(!nxcast_size_grow(65u, 65u, 8u, 64u, &value));
    assert(!nxcast_size_grow(0u, 1u, 0u, 64u, &value));

    puts("checked size tests passed");
    return 0;
}
