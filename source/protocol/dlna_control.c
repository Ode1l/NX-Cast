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
// File identity disabled - commenting out unused directory paths
// static const char g_dlnaIdentityDirParent[] = "sdmc:/switch";
// static const char g_dlnaIdentityDir[] = "sdmc:/switch/NX-Cast";
// static const char g_dlnaIdentityPath[] = "sdmc:/switch/NX-Cast/device_identity.txt";
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

// static char *dlna_strdup_printf(const char *fmt, ...)
// {
//     va_list args;
//     va_list args_copy;
//     int needed;
//     char *buffer;

//     if (!fmt)
//         return NULL;

//     va_start(args, fmt);
//     va_copy(args_copy, args);
//     needed = vsnprintf(NULL, 0, fmt, args_copy);
//     va_end(args_copy);
//     if (needed < 0)
//     {
//         va_end(args);
//         return NULL;
//     }

//     buffer = malloc((size_t)needed + 1);
//     if (!buffer)
//     {
//         va_end(args);
//         return NULL;
//     }

//     vsnprintf(buffer, (size_t)needed + 1, fmt, args);
//     va_end(args);
//     return buffer;
// }

// static bool dlna_ensure_directory(const char *path)
// {
//     if (!path || path[0] == '\0')
//         return false;
//
//     if (mkdir(path, 0777) == 0 || errno == EEXIST)
//         return true;
//
//     log_warn("[dlna] mkdir failed path=%s errno=%d\n", path, errno);
//     return false;
// }

// static char *dlna_generate_uuid_alloc(void)
// {
//     unsigned char raw[16];

//     randomGet(raw, sizeof(raw));
//     raw[6] = (unsigned char)((raw[6] & 0x0f) | 0x40);
//     raw[8] = (unsigned char)((raw[8] & 0x3f) | 0x80);

//     return dlna_strdup_printf("uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
//                               raw[0], raw[1], raw[2], raw[3],
//                               raw[4], raw[5],
//                               raw[6], raw[7],
//                               raw[8], raw[9],
//                               raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
// }

// static char *dlna_generate_serial_alloc(void)
// {
//     unsigned long long value = randomGet64() % 1000000000000ULL;
//     if (value == 0ULL)
//         value = 1ULL;
//     return dlna_strdup_printf("%012llu", value);
// }

// static bool dlna_store_identity_file(const char *uuid, const char *serial_number)
// {
//     (void)uuid;
//     (void)serial_number;
//     // File identity storage disabled to prevent system crashes
//     log_warn("[dlna] identity file storage disabled\n");
//     return true;
// }

// static bool dlna_parse_identity_file(char **uuid_out, char **serial_out)
// {
//     (void)uuid_out;
//     (void)serial_out;
//     // File identity parsing disabled to prevent system crashes
//     log_warn("[dlna] identity file parsing disabled\n");
//     return false;
// }

// static void dlna_identity_ensure_loaded(void)
// {
//     char *uuid = NULL;
//     char *serial = NULL;

//     if (g_dlnaRuntimeUuid && g_dlnaRuntimeSerialNumber)
//         return;

//     if (!dlna_parse_identity_file(&uuid, &serial))
//     {
//         uuid = dlna_generate_uuid_alloc();
//         serial = dlna_generate_serial_alloc();
//         if (uuid && serial)
//         {
//             if (dlna_store_identity_file(uuid, serial))
//                 log_info("[dlna] generated persistent device identity.\n");
//             else
//                 log_warn("[dlna] generated device identity but could not persist it.\n");
//         }
//     }
//     else
//         log_info("[dlna] loaded persistent device identity.\n");

//     if (uuid && serial)
//     {
//         free(g_dlnaRuntimeUuid);
//         free(g_dlnaRuntimeSerialNumber);
//         g_dlnaRuntimeUuid = uuid;
//         g_dlnaRuntimeSerialNumber = serial;
//         return;
//     }

//     free(uuid);
//     free(serial);
//     log_warn("[dlna] using fallback static device identity.\n");
// }

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
