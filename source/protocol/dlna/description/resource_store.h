#pragma once

#include <stdbool.h>

#define DLNA_STORAGE_DIR_PARENT "sdmc:/switch"
#define DLNA_STORAGE_DIR "sdmc:/switch/NX-Cast"
#define DLNA_STORAGE_DLNA_DIR "sdmc:/switch/NX-Cast/dlna"

const char *dlna_resource_store_root(void);
bool dlna_resource_store_ensure_defaults(void);
