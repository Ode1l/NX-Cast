#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const char *friendly_name;
    const char *manufacturer;
    const char *model_name;
    const char *uuid;
} ScpdConfig;

// Start SCPD HTTP server for device.xml + SCPD files.
bool scpd_start(uint16_t port, const ScpdConfig *config);

// Stop SCPD HTTP server.
void scpd_stop(void);
