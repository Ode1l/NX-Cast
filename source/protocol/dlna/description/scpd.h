#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *url_base;
    const char *friendly_name;
    const char *manufacturer;
    const char *manufacturer_url;
    const char *model_description;
    const char *model_name;
    const char *model_number;
    const char *model_url;
    const char *serial_number;
    const char *uuid;
    const char *header_extra;
    const char *service_extra;
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
