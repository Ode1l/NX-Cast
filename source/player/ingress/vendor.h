#pragma once

#include <stdbool.h>

#include "player/ingress.h"

PlayerMediaVendor ingress_detect_vendor(const char *uri, const char *metadata);
bool ingress_vendor_is_sensitive(PlayerMediaVendor vendor);
