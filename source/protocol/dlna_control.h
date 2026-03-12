#pragma once

#include "network/discovery.h"

// Update DLNA control state using the latest discovery results.
void dlna_update_from_discovery(const DiscoveryResults *results);
