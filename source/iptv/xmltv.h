#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "iptv/iptv.h"

int iptv_xmltv_apply_file(const char *path,
                          uint32_t source_id,
                          IptvChannel *channels,
                          int channel_count,
                          time_t now,
                          char *error,
                          size_t error_size);
