#include "dlna_control.h"

#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"
#include "protocol/dlna/control/event_server.h"
#include "protocol/dlna/control/soap_server.h"
#include "protocol/dlna/description/scpd.h"
#include "protocol/dlna/description/template_resource.h"
#include "protocol/dlna/discovery/ssdp.h"
#include "protocol/http/http_server.h"

static bool g_dlnaRunning = false;
static const uint16_t g_dlnaHttpPort = 49152;
static const char g_dlnaDeviceType[] = "urn:schemas-upnp-org:device:MediaRenderer:1";
static const char g_dlnaFriendlyName[] = "NX-Cast";
static const char g_dlnaManufacturer[] = "Ode1l";
static const char g_dlnaModelDescription[] = "Nintendo Switch DLNA Media Renderer";
static const char g_dlnaModelName[] = "NX-Cast Virtual Renderer";
static const char g_dlnaModelNumber[] = "0.1.0";
static const char g_dlnaFallbackSerialNumber[] = "000000000001";
static const char g_dlnaFallbackUuid[] = "uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001";
static const char g_dlnaLocationPath[] = "/description.xml";
// static const char g_dlnaIdentityDir[] = "sdmc:/switch/NX-Cast";
static char *g_dlnaRuntimeSerialNumber = NULL;
static char *g_dlnaRuntimeUuid = NULL;

static const char *dlna_identity_serial_number(void)
{
    return g_dlnaRuntimeSerialNumber ? g_dlnaRuntimeSerialNumber : g_dlnaFallbackSerialNumber;
}

static const char *dlna_identity_uuid(void)
{
    return g_dlnaRuntimeUuid ? g_dlnaRuntimeUuid : g_dlnaFallbackUuid;
}

static char *dlna_determine_local_ip_alloc(void)
{
    int sock;
    struct sockaddr_in remote;
    struct sockaddr_in local;
    socklen_t addr_len = sizeof(local);
    char *local_ip;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return NULL;

    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(9);
    remote.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) < 0)
    {
        close(sock);
        return NULL;
    }

    memset(&local, 0, sizeof(local));
    if (getsockname(sock, (struct sockaddr *)&local, &addr_len) < 0)
    {
        close(sock);
        return NULL;
    }

    local_ip = malloc(INET_ADDRSTRLEN);
    if (!local_ip)
    {
        close(sock);
        return NULL;
    }

    if (!inet_ntop(AF_INET, &local.sin_addr, local_ip, INET_ADDRSTRLEN))
    {
        free(local_ip);
        close(sock);
        return NULL;
    }

    close(sock);
    return local_ip;
}

static char *dlna_build_url_base_alloc(uint16_t http_port)
{
    char *local_ip = dlna_determine_local_ip_alloc();
    char *url_base = NULL;

    if (!local_ip)
        return NULL;

    int needed = snprintf(NULL, 0, "http://%s:%u/", local_ip, http_port);
    if (needed >= 0)
    {
        url_base = malloc((size_t)needed + 1);
        if (url_base)
            snprintf(url_base, (size_t)needed + 1, "http://%s:%u/", local_ip, http_port);
    }

    free(local_ip);
    return url_base;
}

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
    char *url_base = NULL;
    const char *identity_uuid;
    const char *identity_serial;
    const char *manufacturer_url;
    const char *model_url;

    if (g_dlnaRunning)
        return true;

    // dlna_identity_ensure_loaded();
    identity_uuid = dlna_identity_uuid();
    identity_serial = dlna_identity_serial_number();
    url_base = dlna_build_url_base_alloc(g_dlnaHttpPort);
    if (!url_base)
        log_warn("[dlna] unable to determine URLBase, Description.xml will use empty URLBase.\n");
    manufacturer_url = url_base ? url_base : "";
    model_url = url_base ? url_base : "";

    const ScpdConfig scpdConfig = {
        .url_base = url_base ? url_base : "",
        .friendly_name = g_dlnaFriendlyName,
        .manufacturer = g_dlnaManufacturer,
        .manufacturer_url = manufacturer_url,
        .model_description = g_dlnaModelDescription,
        .model_name = g_dlnaModelName,
        .model_number = g_dlnaModelNumber,
        .model_url = model_url,
        .serial_number = identity_serial,
        .uuid = identity_uuid,
        .header_extra = "",
        .service_extra = ""
    };

    const SsdpConfig ssdpConfig = {
        .device_type = g_dlnaDeviceType,
        .friendly_name = g_dlnaFriendlyName,
        .manufacturer = g_dlnaManufacturer,
        .model_name = g_dlnaModelName,
        .uuid = identity_uuid,
        .location_path = g_dlnaLocationPath,
        .http_port = g_dlnaHttpPort
    };

    if (!scpd_start(&scpdConfig))
    {
        free(url_base);
        log_error("[dlna] SCPD module failed to start.\n");
        return false;
    }
    free(url_base);

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

    log_info("[dlna] stop begin\n");
    log_info("[dlna] stop step=ssdp_stop begin\n");
    ssdp_stop();
    log_info("[dlna] stop step=ssdp_stop done\n");
    log_info("[dlna] stop step=http_server_stop begin\n");
    http_server_stop();
    log_info("[dlna] stop step=http_server_stop done\n");
    log_info("[dlna] stop step=event_server_stop begin\n");
    event_server_stop();
    log_info("[dlna] stop step=event_server_stop done\n");
    log_info("[dlna] stop step=soap_server_stop begin\n");
    soap_server_stop();
    log_info("[dlna] stop step=soap_server_stop done\n");
    log_info("[dlna] stop step=scpd_stop begin\n");
    scpd_stop();
    log_info("[dlna] stop step=scpd_stop done\n");
    g_dlnaRunning = false;
    log_info("[dlna] Control layer stopped.\n");
}
