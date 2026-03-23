#pragma once

#include <stdbool.h>
#include <stdint.h>

// Start HTTP description server for device.xml + SCPD files.
bool dlna_description_start(uint16_t port);

// Stop HTTP description server.
void dlna_description_stop(void);
