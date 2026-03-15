#pragma once

#include "protocol/dlna/discovery/ssdp.h"

// Update DLNA control state using the latest discovery results.
void dlna_update_from_discovery(const DlnaDiscoveryResults *results);
