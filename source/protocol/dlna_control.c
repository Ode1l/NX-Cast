#include "dlna_control.h"

#include "log/log.h"
#include "protocol/dlna/control/event_server.h"
#include "protocol/dlna/control/soap_server.h"
#include "protocol/dlna/description/scpd.h"
#include "protocol/dlna/discovery/ssdp.h"
#include "protocol/http/http_server.h"

static bool g_dlnaRunning = false;
static const uint16_t g_dlnaHttpPort = 49152;
static const char g_dlnaDeviceType[] = "urn:schemas-upnp-org:device:MediaRenderer:1";
static const char g_dlnaFriendlyName[] = "NX-Cast";
static const char g_dlnaManufacturer[] = "Ode1l";
static const char g_dlnaModelName[] = "NX-Cast Virtual Renderer";
static const char g_dlnaUuid[] = "uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001";
static const char g_dlnaLocationPath[] = "/device.xml";

static bool dlna_http_dispatch(const HttpRequestContext *ctx,
                               char *response,
                               size_t response_size,
                               size_t *response_len,
                               void *user_data)
{
    (void)user_data;

    if (!ctx || !response || !response_len)
        return false;

    if (soap_server_try_handle_http(ctx->method,
                                    ctx->path,
                                    ctx->request,
                                    ctx->request_len,
                                    response,
                                    response_size,
                                    response_len))
    {
        return true;
    }

    if (event_server_try_handle_http(ctx,
                                     response,
                                     response_size,
                                     response_len))
    {
        return true;
    }

    if (scpd_try_handle_http(ctx->method,
                             ctx->path,
                             response,
                             response_size,
                             response_len))
    {
        return true;
    }

    return false;
}

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

    if (!scpd_start(&scpdConfig))
    {
        log_error("[dlna] SCPD module failed to start.\n");
        return false;
    }

    if (!soap_server_start())
    {
        log_error("[dlna] SOAP control module failed to start.\n");
        scpd_stop();
        return false;
    }

    if (!event_server_start())
    {
        log_error("[dlna] Event control module failed to start.\n");
        soap_server_stop();
        scpd_stop();
        return false;
    }

    const HttpServerConfig httpConfig = {
        .port = g_dlnaHttpPort,
        .handler = dlna_http_dispatch,
        .user_data = NULL
    };

    if (!http_server_start(&httpConfig))
    {
        log_error("[dlna] HTTP server failed to start.\n");
        event_server_stop();
        soap_server_stop();
        scpd_stop();
        return false;
    }

    if (!ssdp_start(&ssdpConfig))
    {
        log_error("[dlna] SSDP responder failed to start.\n");
        http_server_stop();
        event_server_stop();
        soap_server_stop();
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
    http_server_stop();
    event_server_stop();
    soap_server_stop();
    scpd_stop();
    g_dlnaRunning = false;
    log_info("[dlna] Control layer stopped.\n");
}
