#include "server_info.h"

#include <switch.h>

#include <stdbool.h>
#include <stdio.h>

#ifndef NXCAST_APP_VERSION
#define NXCAST_APP_VERSION "0.1.0"
#endif

const char *dlna_server_info_get(void)
{
    static char server_info[96];
    static bool initialized = false;

    if (!initialized)
    {
        u32 hos = hosversionGet();

        if (hos == 0)
        {
            snprintf(server_info,
                     sizeof(server_info),
                     "NintendoSwitch/unknown UPnP/1.0 NX-Cast/%s",
                     NXCAST_APP_VERSION);
        }
        else
        {
            snprintf(server_info,
                     sizeof(server_info),
                     "NintendoSwitch/%u.%u.%u UPnP/1.0 NX-Cast/%s",
                     (unsigned)HOSVER_MAJOR(hos),
                     (unsigned)HOSVER_MINOR(hos),
                     (unsigned)HOSVER_MICRO(hos),
                     NXCAST_APP_VERSION);
        }

        initialized = true;
    }

    return server_info;
}
