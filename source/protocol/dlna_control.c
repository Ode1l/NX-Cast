#include "dlna_control.h"

#include "log/log.h"
#include "protocol/dlna/description/scpd.h"
#include "protocol/dlna/discovery/ssdp.h"

static bool g_dlnaRunning = false;
static const uint16_t g_dlnaHttpPort = 49152;
static const char g_dlnaDeviceType[] = "urn:schemas-upnp-org:device:MediaRenderer:1";
static const char g_dlnaFriendlyName[] = "NX-Cast";
static const char g_dlnaManufacturer[] = "Ode1l";
static const char g_dlnaModelName[] = "NX-Cast Virtual Renderer";
static const char g_dlnaUuid[] = "uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001";
static const char g_dlnaLocationPath[] = "/device.xml";

bool dlna_control_start(void)
{
    if (g_dlnaRunning)
        return true;

    const ScpdConfig scpdConfig = {
        .friendly_name = g_dlnaFriendlyName,
        .manufacturer = g_dlnaManufacturer,
        .model_name = g_dlnaModelName,
        .uuid = g_dlnaUuid
    };

    const SsdpConfig ssdpConfig = {
        .device_type = g_dlnaDeviceType,
        .friendly_name = g_dlnaFriendlyName,
        .manufacturer = g_dlnaManufacturer,
        .model_name = g_dlnaModelName,
        .uuid = g_dlnaUuid,
        .location_path = g_dlnaLocationPath,
        .http_port = g_dlnaHttpPort
    };

    if (!scpd_start(g_dlnaHttpPort, &scpdConfig))
    {
        log_error("[dlna] SCPD HTTP server failed to start.\n");
        return false;
    }

    if (!ssdp_start(&ssdpConfig))
    {
        log_error("[dlna] SSDP responder failed to start.\n");
        scpd_stop();
        return false;
    }

    g_dlnaRunning = true;
    log_info("[dlna] Control layer initialized (HTTP/SCPD on :%u).\n", g_dlnaHttpPort);
    return true;
}

void dlna_control_stop(void)
{
    if (!g_dlnaRunning)
        return;

    ssdp_stop();
    scpd_stop();
    g_dlnaRunning = false;
    log_info("[dlna] Control layer stopped.\n");
}
