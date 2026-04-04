#pragma once

#include <stdbool.h>

#include "player/ingress.h"

void ingress_select_metadata_resource(const char *input_uri,
                                      PlayerMedia *media,
                                      bool *likely_segmented,
                                      PlayerMediaVendor vendor);
