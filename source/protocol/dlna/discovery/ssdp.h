#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const char *device_type;
    const char *friendly_name;
    const char *manufacturer;
    const char *model_name;
    const char *uuid;
    const char *location_path;
    uint16_t http_port;
} SsdpConfig;

bool ssdp_start(const SsdpConfig *config);
void ssdp_stop(void);
