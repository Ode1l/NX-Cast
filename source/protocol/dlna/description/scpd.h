#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *friendly_name;
    const char *manufacturer;
    const char *model_name;
    const char *uuid;
} ScpdConfig;

// Initialize SCPD resources (device.xml + service SCPD files).
bool scpd_start(const ScpdConfig *config);

// Shutdown SCPD resources.
void scpd_stop(void);

// Try handling a description request.
// Returns true if the request belongs to SCPD/device description and a response was built.
bool scpd_try_handle_http(const char *method,
                          const char *path,
                          char *response,
                          size_t response_size,
                          size_t *response_len);
