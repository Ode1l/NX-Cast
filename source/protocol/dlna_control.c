#include "dlna_control.h"

#include "log/log.h"
#include "protocol/dlna/discovery/ssdp.h"

static bool g_dlnaRunning = false;

bool dlna_control_start(void)
{
    if (g_dlnaRunning)
        return true;

    static const SsdpConfig config = {
        .device_type = "urn:schemas-upnp-org:device:MediaRenderer:1",
        .friendly_name = "NX-Cast",
        .manufacturer = "Ode1l",
        .model_name = "NX-Cast Virtual Renderer",
        .uuid = "uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001",
        .location_path = "/device.xml",
        .http_port = 49152
    };

    if (!ssdp_start(&config))
    {
        log_error("[dlna] SSDP responder failed to start.\n");
        return false;
    }

    g_dlnaRunning = true;
    log_info("[dlna] Control layer initialized.\n");
    return true;
}

void dlna_control_stop(void)
{
    if (!g_dlnaRunning)
        return;

    ssdp_stop();
    g_dlnaRunning = false;
    log_info("[dlna] Control layer stopped.\n");
}
