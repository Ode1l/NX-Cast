#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*IptvFetchCancelled)(void *context);

typedef struct
{
    IptvFetchCancelled cancelled;
    void *context;
} IptvFetchControl;

bool iptv_fetch_to_file(const char *url,
                        const char *destination,
                        size_t maximum_size,
                        const IptvFetchControl *control,
                        char *error,
                        size_t error_size);
