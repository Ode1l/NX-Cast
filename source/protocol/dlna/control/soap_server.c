#include "soap_server.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "soap_router.h"
#include "handler.h"
#include "log/log.h"

static bool g_running = false;
#define SOAP_LOG_PREVIEW_MAX 512
#define SOAP_LOG_CHUNK_MAX 700

static bool starts_with(const char *value, const char *prefix)
{
    return value && prefix && strncmp(value, prefix, strlen(prefix)) == 0;
}

static void trim_whitespace(char *value)
{
    if (!value)
        return;

    size_t len = strlen(value);
    while (len > 0 && isspace((unsigned char)value[len - 1]))
        value[--len] = '\0';

    size_t start = 0;
    while (value[start] && isspace((unsigned char)value[start]))
        ++start;

    if (start > 0)
        memmove(value, value + start, strlen(value + start) + 1);
}

static void unquote(char *value)
{
    trim_whitespace(value);
    size_t len = strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"')
    {
        memmove(value, value + 1, len - 2);
        value[len - 2] = '\0';
    }
}

static bool get_header_value(const char *request, const char *header, char *out, size_t out_size)
{
    if (!request || !header || !out || out_size == 0)
        return false;

    size_t header_len = strlen(header);
    const char *cursor = request;

    while (*cursor)
    {
        const char *line_end = strstr(cursor, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (line_len == 0)
            break;

        if (line_len > header_len + 1 &&
            strncasecmp(cursor, header, header_len) == 0 &&
            cursor[header_len] == ':')
        {
            const char *value_start = cursor + header_len + 1;
            while (*value_start == ' ' || *value_start == '\t')
                ++value_start;

            size_t copy_len = line_end ? (size_t)(line_end - value_start) : strlen(value_start);
            if (copy_len >= out_size)
                copy_len = out_size - 1;

            memcpy(out, value_start, copy_len);
            out[copy_len] = '\0';
            trim_whitespace(out);
            return true;
        }

        if (!line_end)
            break;
        cursor = line_end + 2;
    }

    return false;
}

static const char *find_body(const char *request)
{
    const char *header_end = strstr(request, "\r\n\r\n");
    if (!header_end)
        return NULL;
    return header_end + 4;
}

static bool parse_action_name(const char *soap_action_header, char *action_name, size_t action_name_size)
{
    if (!soap_action_header || !action_name || action_name_size == 0)
        return false;

    char value[256];
    snprintf(value, sizeof(value), "%s", soap_action_header);
    unquote(value);

    const char *hash = strrchr(value, '#');
    if (!hash || hash[1] == '\0')
        return false;

    size_t len = strlen(hash + 1);
    if (len >= action_name_size)
        len = action_name_size - 1;

    memcpy(action_name, hash + 1, len);
    action_name[len] = '\0';
    return true;
}

static bool parse_service_name_from_path(const char *path, char *service_name, size_t service_name_size)
{
    if (!starts_with(path, "/upnp/control/") || !service_name || service_name_size == 0)
        return false;

    const char *name = path + strlen("/upnp/control/");
    size_t len = strcspn(name, " ?\r\n");
    if (len == 0)
        return false;

    if (len >= service_name_size)
        len = service_name_size - 1;

    memcpy(service_name, name, len);
    service_name[len] = '\0';
    return true;
}

static void log_payload_preview(const char *label, const char *payload)
{
    if (!label)
        return;
    if (!payload)
    {
        log_debug("[soap] %s: (null)\n", label);
        return;
    }

    size_t len = strlen(payload);
    if (log_get_level() == LOG_LEVEL_DEBUG && log_get_verbose_payload())
    {
        // Print full payload in chunks so each log entry stays under LOG_MESSAGE_MAX.
        log_debug("[soap] %s len=%zu (full)\n", label, len);
        size_t offset = 0;
        while (offset < len)
        {
            size_t chunk_len = len - offset;
            if (chunk_len > SOAP_LOG_CHUNK_MAX)
                chunk_len = SOAP_LOG_CHUNK_MAX;

            char chunk[SOAP_LOG_CHUNK_MAX + 1];
            memcpy(chunk, payload + offset, chunk_len);
            chunk[chunk_len] = '\0';

            log_debug("[soap] %s chunk[%zu:%zu]: %s\n",
                      label,
                      offset,
                      offset + chunk_len,
                      chunk);
            offset += chunk_len;
        }
        return;
    }

    size_t preview_len = len;
    bool truncated = false;
    if (preview_len > SOAP_LOG_PREVIEW_MAX)
    {
        preview_len = SOAP_LOG_PREVIEW_MAX;
        truncated = true;
    }

    char preview[SOAP_LOG_PREVIEW_MAX + 1];
    memcpy(preview, payload, preview_len);
    preview[preview_len] = '\0';

    for (size_t i = 0; i < preview_len; ++i)
    {
        if (preview[i] == '\r' || preview[i] == '\n' || preview[i] == '\t')
            preview[i] = ' ';
    }

    log_debug("[soap] %s len=%zu%s: %s\n",
              label,
              len,
              truncated ? " (truncated)" : "",
              preview);
}

static bool build_http_response(int status,
                                const char *status_text,
                                const char *content_type,
                                const char *body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    if (!status_text || !content_type || !body || !response || response_size == 0 || !response_len)
        return false;

    size_t body_len = strlen(body);
    int written = snprintf(response, response_size,
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           status,
                           status_text,
                           content_type,
                           body_len,
                           body);

    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    return true;
}

static bool build_soap_success(const char *service_type,
                               const char *action_name,
                               const char *response_args,
                               char *response,
                               size_t response_size,
                               size_t *response_len)
{
    if (!service_type || !action_name)
        return false;

    char body[4096];
    int body_written = snprintf(body, sizeof(body),
                                "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                                "<s:Body>"
                                "<u:%sResponse xmlns:u=\"%s\">%s</u:%sResponse>"
                                "</s:Body>"
                                "</s:Envelope>",
                                action_name,
                                service_type,
                                response_args ? response_args : "",
                                action_name);

    if (body_written < 0 || (size_t)body_written >= sizeof(body))
        return false;

    return build_http_response(200,
                               "OK",
                               "text/xml; charset=\"utf-8\"",
                               body,
                               response,
                               response_size,
                               response_len);
}

static bool build_soap_fault(int fault_code,
                             const char *fault_description,
                             char *response,
                             size_t response_size,
                             size_t *response_len)
{
    const char *description = fault_description ? fault_description : "Action Failed";

    char body[4096];
    int body_written = snprintf(body, sizeof(body),
                                "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                                "<s:Body>"
                                "<s:Fault>"
                                "<faultcode>s:Client</faultcode>"
                                "<faultstring>UPnPError</faultstring>"
                                "<detail>"
                                "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
                                "<errorCode>%d</errorCode>"
                                "<errorDescription>%s</errorDescription>"
                                "</UPnPError>"
                                "</detail>"
                                "</s:Fault>"
                                "</s:Body>"
                                "</s:Envelope>",
                                fault_code,
                                description);

    if (body_written < 0 || (size_t)body_written >= sizeof(body))
        return false;

    return build_http_response(500,
                               "Internal Server Error",
                               "text/xml; charset=\"utf-8\"",
                               body,
                               response,
                               response_size,
                               response_len);
}

bool soap_server_start(void)
{
    if (g_running)
        return true;

    soap_handler_init();
    g_running = true;
    log_info("[soap] SOAP control module started.\n");
    return true;
}

void soap_server_stop(void)
{
    if (!g_running)
        return;

    soap_handler_shutdown();
    g_running = false;
    log_info("[soap] SOAP control module stopped.\n");
}

bool soap_server_try_handle_http(const char *method,
                          const char *path,
                          const char *request,
                          size_t request_len,
                          char *response,
                          size_t response_size,
                          size_t *response_len)
{
    if (!path || !response || !response_len)
        return false;

    *response_len = 0;

    if (!starts_with(path, "/upnp/control/"))
        return false;

    if (!g_running)
    {
        return build_http_response(503,
                                   "Service Unavailable",
                                   "text/plain; charset=\"utf-8\"",
                                   "SOAP module is not running",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!method || strcasecmp(method, "POST") != 0)
    {
        return build_http_response(405,
                                   "Method Not Allowed",
                                   "text/plain; charset=\"utf-8\"",
                                   "SOAP control requires POST",
                                   response,
                                   response_size,
                                   response_len);
    }

    if (!request)
    {
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char service_name[64];
    if (!parse_service_name_from_path(path, service_name, sizeof(service_name)))
    {
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char soap_action_header[256];
    if (!get_header_value(request, "SOAPACTION", soap_action_header, sizeof(soap_action_header)))
    {
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char action_name[64];
    if (!parse_action_name(soap_action_header, action_name, sizeof(action_name)))
    {
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    const char *body = find_body(request);
    if (!body)
        body = "";

    char host_header[128];
    host_header[0] = '\0';
    get_header_value(request, "Host", host_header, sizeof(host_header));

    log_debug("[soap] HTTP %s http://%s%s bytes=%zu\n",
              method ? method : "(null)",
              host_header[0] ? host_header : "(no-host)",
              path,
              request_len);
    log_debug("[soap] route service=%s action=%s soapAction=%s\n",
              service_name, action_name, soap_action_header);
    log_payload_preview("request body xml", body);

    SoapActionContext ctx = {
        .service_name = service_name,
        .action_name = action_name,
        .body = body
    };

    SoapRouteResult result;
    bool handled_ok = soap_router_route_action(&ctx, &result);
    if (handled_ok && result.output.success)
    {
        log_payload_preview("response args xml", result.output.output_xml);

        bool built = build_soap_success(result.service_type,
                                        result.action_name,
                                        result.output.output_xml,
                                        response,
                                        response_size,
                                        response_len);
        if (!built)
        {
            log_error("[soap] failed to build success response for %s#%s\n",
                      service_name, action_name);
            return false;
        }

        const char *response_body = find_body(response);
        log_payload_preview("response body xml", response_body ? response_body : "");
        return true;
    }

    int fault_code = result.output.fault_code > 0 ? result.output.fault_code : 501;
    const char *fault_description = result.output.fault_description ? result.output.fault_description : "Action Failed";
    log_warn("[soap] action fault service=%s action=%s code=%d desc=%s\n",
             service_name, action_name, fault_code, fault_description);
    bool built = build_soap_fault(fault_code,
                                  fault_description,
                                  response,
                                  response_size,
                                  response_len);
    if (!built)
    {
        log_error("[soap] failed to build fault response for %s#%s\n",
                  service_name, action_name);
        return false;
    }

    const char *fault_body = find_body(response);
    log_payload_preview("fault body xml", fault_body ? fault_body : "");
    return true;
}
