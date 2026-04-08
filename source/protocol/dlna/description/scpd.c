#include "scpd.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log/log.h"
#include "template_resource.h"

#define SCPD_RENDER_BUFFER_SIZE 65536

typedef struct
{
    const char *request_path;
    const char *template_path;
    const char *content_type;
    bool use_template_values;
} ScpdRoute;

static const char g_defaultFriendlyName[] = "NX-Cast";
static const char g_defaultManufacturer[] = "Ode1l";
static const char g_defaultManufacturerUrl[] = "";
static const char g_defaultModelDescription[] = "Nintendo Switch DLNA Media Renderer";
static const char g_defaultModelName[] = "NX-Cast Virtual Renderer";
static const char g_defaultModelNumber[] = "0.1.0";
static const char g_defaultModelUrl[] = "";
static const char g_defaultSerialNumber[] = "00000001";
static const char g_defaultUuid[] = "6b0d3c60-3d96-41f4-986c-0a4bb12b0001";
static const char g_defaultHeaderExtra[] = "";
static const char g_defaultServiceExtra[] = "";

static const ScpdRoute g_scpd_routes[] = {
    {"/Description.xml", "Description.xml", "application/xml; charset=\"utf-8\"", true},
    {"/device.xml", "Description.xml", "application/xml; charset=\"utf-8\"", true},
    {"/dlna/AVTransport.xml", "AVTransport.xml", "application/xml; charset=\"utf-8\"", false},
    {"/scpd/AVTransport.xml", "AVTransport.xml", "application/xml; charset=\"utf-8\"", false},
    {"/dlna/RenderingControl.xml", "RenderingControl.xml", "application/xml; charset=\"utf-8\"", false},
    {"/scpd/RenderingControl.xml", "RenderingControl.xml", "application/xml; charset=\"utf-8\"", false},
    {"/dlna/ConnectionManager.xml", "ConnectionManager.xml", "application/xml; charset=\"utf-8\"", false},
    {"/scpd/ConnectionManager.xml", "ConnectionManager.xml", "application/xml; charset=\"utf-8\"", false},
};

static bool g_running = false;
static DlnaTemplateValues g_template_values = {0};

static const char *coalesce_string(const char *value, const char *fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return value;
}

static bool scpd_dup_string(char **slot, const char *value)
{
    char *copy;

    if (!slot)
        return false;

    copy = strdup(value ? value : "");
    if (!copy)
        return false;

    free(*slot);
    *slot = copy;
    return true;
}

static bool scpd_dup_uuid_without_prefix(char **slot, const char *value)
{
    const char *normalized = value ? value : "";
    if (strncmp(normalized, "uuid:", 5) == 0)
        normalized += 5;
    return scpd_dup_string(slot, normalized);
}

static void scpd_clear_template_values(void)
{
    free((char *)g_template_values.friendly_name);
    free((char *)g_template_values.manufacturer);
    free((char *)g_template_values.manufacturer_url);
    free((char *)g_template_values.model_description);
    free((char *)g_template_values.model_name);
    free((char *)g_template_values.model_number);
    free((char *)g_template_values.model_url);
    free((char *)g_template_values.serial_number);
    free((char *)g_template_values.uuid);
    free((char *)g_template_values.header_extra);
    free((char *)g_template_values.service_extra);
    memset(&g_template_values, 0, sizeof(g_template_values));
}

static bool scpd_apply_config(const ScpdConfig *config)
{
    const char *friendly_name = coalesce_string(config ? config->friendly_name : NULL, g_defaultFriendlyName);
    const char *manufacturer = coalesce_string(config ? config->manufacturer : NULL, g_defaultManufacturer);
    const char *manufacturer_url = coalesce_string(config ? config->manufacturer_url : NULL, g_defaultManufacturerUrl);
    const char *model_description = coalesce_string(config ? config->model_description : NULL, g_defaultModelDescription);
    const char *model_name = coalesce_string(config ? config->model_name : NULL, g_defaultModelName);
    const char *model_number = coalesce_string(config ? config->model_number : NULL, g_defaultModelNumber);
    const char *model_url = coalesce_string(config ? config->model_url : NULL, g_defaultModelUrl);
    const char *serial_number = coalesce_string(config ? config->serial_number : NULL, g_defaultSerialNumber);
    const char *uuid = coalesce_string(config ? config->uuid : NULL, g_defaultUuid);
    const char *header_extra = coalesce_string(config ? config->header_extra : NULL, g_defaultHeaderExtra);
    const char *service_extra = coalesce_string(config ? config->service_extra : NULL, g_defaultServiceExtra);

    scpd_clear_template_values();

    return scpd_dup_string((char **)&g_template_values.friendly_name, friendly_name) &&
           scpd_dup_string((char **)&g_template_values.manufacturer, manufacturer) &&
           scpd_dup_string((char **)&g_template_values.manufacturer_url, manufacturer_url) &&
           scpd_dup_string((char **)&g_template_values.model_description, model_description) &&
           scpd_dup_string((char **)&g_template_values.model_name, model_name) &&
           scpd_dup_string((char **)&g_template_values.model_number, model_number) &&
           scpd_dup_string((char **)&g_template_values.model_url, model_url) &&
           scpd_dup_string((char **)&g_template_values.serial_number, serial_number) &&
           scpd_dup_uuid_without_prefix((char **)&g_template_values.uuid, uuid) &&
           scpd_dup_string((char **)&g_template_values.header_extra, header_extra) &&
           scpd_dup_string((char **)&g_template_values.service_extra, service_extra);
}

static const ScpdRoute *scpd_find_route(const char *path)
{
    if (!path)
        return NULL;

    for (size_t i = 0; i < sizeof(g_scpd_routes) / sizeof(g_scpd_routes[0]); ++i)
    {
        if (strcmp(path, g_scpd_routes[i].request_path) == 0)
            return &g_scpd_routes[i];
    }

    return NULL;
}

static bool build_http_response(int status,
                                const char *status_text,
                                const char *content_type,
                                const char *body,
                                size_t body_len,
                                bool include_body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    int written;

    if (!status_text || !content_type || !response || response_size == 0 || !response_len)
        return false;

    written = snprintf(response, response_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: %s\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       status,
                       status_text,
                       content_type,
                       body_len);
    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    if (!include_body || !body || body_len == 0)
        return true;

    if (*response_len + body_len >= response_size)
        return false;

    memcpy(response + *response_len, body, body_len);
    *response_len += body_len;
    response[*response_len] = '\0';
    return true;
}

static bool build_text_response(int status,
                                const char *status_text,
                                const char *body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    size_t body_len = body ? strlen(body) : 0;
    return build_http_response(status,
                               status_text,
                               "text/plain; charset=\"utf-8\"",
                               body ? body : "",
                               body_len,
                               true,
                               response,
                               response_size,
                               response_len);
}

bool scpd_start(const ScpdConfig *config)
{
    if (g_running)
        return true;

    if (!scpd_apply_config(config))
    {
        scpd_clear_template_values();
        return false;
    }

    g_running = true;
    log_info("[scpd] description templates ready.\n");
    return true;
}

void scpd_stop(void)
{
    if (!g_running)
        return;

    scpd_clear_template_values();
    g_running = false;
    log_info("[scpd] description templates stopped.\n");
}

bool scpd_try_handle_http(const char *method,
                          const char *path,
                          char *response,
                          size_t response_size,
                          size_t *response_len)
{
    char rendered[SCPD_RENDER_BUFFER_SIZE];
    size_t rendered_len = 0;
    const ScpdRoute *route;
    bool include_body;

    if (!path || !response || !response_len)
        return false;

    *response_len = 0;

    route = scpd_find_route(path);
    if (!route)
        return false;

    if (!g_running)
    {
        return build_text_response(503,
                                   "Service Unavailable",
                                   "SCPD module is not running",
                                   response,
                                   response_size,
                                   response_len);
    }

    include_body = method && strcmp(method, "HEAD") != 0;
    if (!method || (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0))
    {
        return build_text_response(405,
                                   "Method Not Allowed",
                                   "SCPD endpoint requires GET or HEAD",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!dlna_template_render_file_to_buffer(route->template_path,
                                             route->use_template_values ? &g_template_values : NULL,
                                             rendered,
                                             sizeof(rendered),
                                             &rendered_len))
    {
        log_error("[scpd] failed to render template path=%s request=%s\n",
                  route->template_path, path);
        return build_text_response(500,
                                   "Internal Server Error",
                                   "Failed to render description template",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!build_http_response(200,
                             "OK",
                             route->content_type,
                             rendered,
                             rendered_len,
                             include_body,
                             response,
                             response_size,
                             response_len))
    {
        log_error("[scpd] failed to build response for path=%s\n", path);
        return false;
    }

    log_info("[scpd] served path=%s bytes=%zu template=%s\n",
             path, rendered_len, route->template_path);
    return true;
}
