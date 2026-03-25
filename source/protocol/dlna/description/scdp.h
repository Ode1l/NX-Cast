#pragma once

#include <stdbool.h>
#include <stdint.h>

// Start SCDP HTTP server for device.xml + SCPD files.
bool scdp_start(uint16_t port);

// Stop SCDP HTTP server.
void scdp_stop(void);
