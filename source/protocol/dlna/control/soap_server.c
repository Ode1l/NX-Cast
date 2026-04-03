#include "soap_server.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "soap_router.h"
#include "handler.h"
#include "log/log.h"

static bool g_running = false;

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

static bool is_blank_text(const char *value)
{
    if (!value)
        return true;

    while (*value)
    {
        if (!isspace((unsigned char)*value))
            return false;
        ++value;
    }
    return true;
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

static bool extract_xml_tag_value(const char *xml, const char *tag, char *out, size_t out_size)
{
    if (!xml || !tag || !out || out_size == 0)
        return false;

    char open_tag[64];
    char close_tag[64];
    int open_len = snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    int close_len = snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
    if (open_len <= 0 || close_len <= 0 ||
        (size_t)open_len >= sizeof(open_tag) || (size_t)close_len >= sizeof(close_tag))
        return false;

    const char *start = strstr(xml, open_tag);
    if (!start)
        return false;
    start += (size_t)open_len;

    const char *end = strstr(start, close_tag);
    if (!end || end < start)
        return false;

    size_t value_len = (size_t)(end - start);
    if (value_len >= out_size)
        value_len = out_size - 1;

    memcpy(out, start, value_len);
    out[value_len] = '\0';
    trim_whitespace(out);
    return true;
}

static void clip_for_log(const char *input, char *output, size_t output_size)
{
    if (!output || output_size == 0)
        return;
    if (!input)
    {
        output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    if (len + 1 <= output_size)
    {
        snprintf(output, output_size, "%s", input);
        return;
    }

    if (output_size <= 4)
    {
        output[0] = '\0';
        return;
    }

    size_t copy_len = output_size - 4;
    memcpy(output, input, copy_len);
    output[copy_len] = '.';
    output[copy_len + 1] = '.';
    output[copy_len + 2] = '.';
    output[copy_len + 3] = '\0';
}

static void log_fault_request_detail(const char *reason,
                                     const char *method,
                                     const char *path,
                                     const char *soap_action,
                                     const char *request,
                                     size_t request_len,
                                     const char *body,
                                     size_t body_len)
{
    char request_line[192];
    char host_header[128];
    char content_length[64];
    char user_agent[128];
    char body_short[384];

    request_line[0] = '\0';
    host_header[0] = '\0';
    content_length[0] = '\0';
    user_agent[0] = '\0';
    body_short[0] = '\0';

    if (request && request[0] != '\0')
    {
        const char *line_end = strstr(request, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - request) : strlen(request);
        if (line_len >= sizeof(request_line))
            line_len = sizeof(request_line) - 1;
        memcpy(request_line, request, line_len);
        request_line[line_len] = '\0';

        get_header_value(request, "Host", host_header, sizeof(host_header));
        get_header_value(request, "Content-Length", content_length, sizeof(content_length));
        get_header_value(request, "User-Agent", user_agent, sizeof(user_agent));
    }

    clip_for_log(body ? body : "", body_short, sizeof(body_short));

    log_warn("[soap] fault detail reason=%s method=%s endpoint=%s soapAction=%s request_line=%s request_bytes=%zu body_bytes=%zu host=%s content-length=%s user-agent=%s\n",
             reason ? reason : "(none)",
             method ? method : "(null)",
             path ? path : "(null)",
             soap_action ? soap_action : "(none)",
             request_line[0] ? request_line : "(none)",
             request_len,
             body_len,
             host_header[0] ? host_header : "(none)",
             content_length[0] ? content_length : "(none)",
             user_agent[0] ? user_agent : "(none)");
    if (body_len > 0)
        log_warn("[soap] fault body=%s\n", body_short);
    else
        log_warn("[soap] fault body=<empty>\n");
}

static void log_soap_call_summary(const char *service_name, const char *action_name, const char *body, size_t body_len)
{
    char instance_id[32];
    char current_uri[256];
    char current_uri_short[96];
    char target[64];
    char unit[32];
    char speed[32];
    char channel[32];
    char desired_volume[32];
    char desired_mute[16];

    bool has_instance_id = extract_xml_tag_value(body, "InstanceID", instance_id, sizeof(instance_id));
    bool has_current_uri = extract_xml_tag_value(body, "CurrentURI", current_uri, sizeof(current_uri));
    bool has_target = extract_xml_tag_value(body, "Target", target, sizeof(target));
    bool has_unit = extract_xml_tag_value(body, "Unit", unit, sizeof(unit));
    bool has_speed = extract_xml_tag_value(body, "Speed", speed, sizeof(speed));
    bool has_channel = extract_xml_tag_value(body, "Channel", channel, sizeof(channel));
    bool has_desired_volume = extract_xml_tag_value(body, "DesiredVolume", desired_volume, sizeof(desired_volume));
    bool has_desired_mute = extract_xml_tag_value(body, "DesiredMute", desired_mute, sizeof(desired_mute));

    if (has_current_uri && has_instance_id)
    {
        clip_for_log(current_uri, current_uri_short, sizeof(current_uri_short));
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID> <CurrentURI>%s</CurrentURI>\n",
                 service_name, action_name, instance_id, current_uri_short);
        return;
    }

    if (has_unit && has_target && has_instance_id)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID> <Unit>%s</Unit> <Target>%s</Target>\n",
                 service_name, action_name, instance_id, unit, target);
        return;
    }

    if (has_speed && has_instance_id)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID> <Speed>%s</Speed>\n",
                 service_name, action_name, instance_id, speed);
        return;
    }

    if (has_desired_volume && has_channel && has_instance_id)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID> <Channel>%s</Channel> <DesiredVolume>%s</DesiredVolume>\n",
                 service_name, action_name, instance_id, channel, desired_volume);
        return;
    }

    if (has_desired_mute && has_channel && has_instance_id)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID> <Channel>%s</Channel> <DesiredMute>%s</DesiredMute>\n",
                 service_name, action_name, instance_id, channel, desired_mute);
        return;
    }

    if (has_instance_id)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <InstanceID>%s</InstanceID>\n",
                 service_name, action_name, instance_id);
        return;
    }

    if (has_current_uri)
    {
        clip_for_log(current_uri, current_uri_short, sizeof(current_uri_short));
        log_info("[soap] soap_call \"%s\" \"%s\" <CurrentURI>%s</CurrentURI>\n",
                 service_name, action_name, current_uri_short);
        return;
    }

    if (has_target && has_unit)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <Unit>%s</Unit> <Target>%s</Target>\n",
                 service_name, action_name, unit, target);
        return;
    }

    if (has_speed)
    {
        log_info("[soap] soap_call \"%s\" \"%s\" <Speed>%s</Speed>\n",
                 service_name, action_name, speed);
        return;
    }

    if (has_desired_volume)
    {
        if (has_channel)
        {
            log_info("[soap] soap_call \"%s\" \"%s\" <Channel>%s</Channel> <DesiredVolume>%s</DesiredVolume>\n",
                     service_name, action_name, channel, desired_volume);
            return;
        }
        log_info("[soap] soap_call \"%s\" \"%s\" <DesiredVolume>%s</DesiredVolume>\n",
                 service_name, action_name, desired_volume);
        return;
    }

    if (has_desired_mute)
    {
        if (has_channel)
        {
            log_info("[soap] soap_call \"%s\" \"%s\" <Channel>%s</Channel> <DesiredMute>%s</DesiredMute>\n",
                     service_name, action_name, channel, desired_mute);
            return;
        }
        log_info("[soap] soap_call \"%s\" \"%s\" <DesiredMute>%s</DesiredMute>\n",
                 service_name, action_name, desired_mute);
        return;
    }

    log_info("[soap] soap_call \"%s\" \"%s\" body_bytes=%zu\n",
             service_name, action_name, body_len);
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

    size_t body_capacity = (response_args ? strlen(response_args) : 0) + 512;
    char *body = malloc(body_capacity);
    if (!body)
        return false;

    int body_written = snprintf(body, body_capacity,
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

    if (body_written < 0 || (size_t)body_written >= body_capacity)
    {
        free(body);
        return false;
    }

    bool built = build_http_response(200,
                                     "OK",
                                     "text/xml; charset=\"utf-8\"",
                                     body,
                                     response,
                                     response_size,
                                     response_len);
    free(body);
    return built;
}

static bool build_soap_fault(int fault_code,
                             const char *fault_description,
                             char *response,
                             size_t response_size,
                             size_t *response_len)
{
    const char *description = fault_description ? fault_description : "Action Failed";

    size_t body_capacity = strlen(description) + 512;
    char *body = malloc(body_capacity);
    if (!body)
        return false;

    int body_written = snprintf(body, body_capacity,
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

    if (body_written < 0 || (size_t)body_written >= body_capacity)
    {
        free(body);
        return false;
    }

    bool built = build_http_response(500,
                                     "Internal Server Error",
                                     "text/xml; charset=\"utf-8\"",
                                     body,
                                     response,
                                     response_size,
                                     response_len);
    free(body);
    return built;
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
        log_fault_request_detail("request_null",
                                 method,
                                 path,
                                 NULL,
                                 request,
                                 request_len,
                                 NULL,
                                 0);
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char service_name[64];
    if (!parse_service_name_from_path(path, service_name, sizeof(service_name)))
    {
        log_fault_request_detail("invalid_service_path",
                                 method,
                                 path,
                                 NULL,
                                 request,
                                 request_len,
                                 NULL,
                                 0);
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char soap_action_header[256];
    if (!get_header_value(request, "SOAPACTION", soap_action_header, sizeof(soap_action_header)))
    {
        const char *body = find_body(request);
        size_t body_len = body ? strlen(body) : 0;
        log_fault_request_detail("missing_SOAPACTION",
                                 method,
                                 path,
                                 NULL,
                                 request,
                                 request_len,
                                 body,
                                 body_len);
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    char action_name[64];
    if (!parse_action_name(soap_action_header, action_name, sizeof(action_name)))
    {
        const char *body = find_body(request);
        size_t body_len = body ? strlen(body) : 0;
        log_fault_request_detail("invalid_SOAPACTION",
                                 method,
                                 path,
                                 soap_action_header,
                                 request,
                                 request_len,
                                 body,
                                 body_len);
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

    const char *body = find_body(request);
    if (!body)
        body = "";
    size_t body_len = strlen(body);

    if (is_blank_text(body))
    {
        log_fault_request_detail("empty_SOAP_body",
                                 method,
                                 path,
                                 soap_action_header,
                                 request,
                                 request_len,
                                 body,
                                 body_len);
        return build_soap_fault(402,
                                "Invalid Args",
                                response,
                                response_size,
                                response_len);
    }

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
    log_debug("[soap] recv packet endpoint=%s body_bytes=%zu\n", path, body_len);
    log_soap_call_summary(service_name, action_name, body, body_len);

    SoapActionContext ctx = {
        .service_name = service_name,
        .action_name = action_name,
        .body = body
    };

    SoapRouteResult *result = calloc(1, sizeof(*result));
    if (!result)
    {
        log_error("[soap] failed to allocate route result\n");
        return false;
    }

    bool handled_ok = soap_router_route_action(&ctx, result);
    if (handled_ok && result->output.success)
    {
        bool built = build_soap_success(result->service_type,
                                        result->action_name,
                                        result->output.output_xml,
                                        response,
                                        response_size,
                                        response_len);
        if (!built)
        {
            log_error("[soap] failed to build success response for %s#%s\n",
                      service_name, action_name);
            free(result);
            return false;
        }

        log_debug("[soap] send packet endpoint=%s status=200 bytes=%zu\n",
                  path, *response_len);
        log_info("[soap] assert_http_200 service=%s action=%s handler=%s\n",
                 service_name, action_name,
                 result->handler_name ? result->handler_name : "(none)");
        free(result);
        return true;
    }

    int fault_code = result->output.fault_code > 0 ? result->output.fault_code : 501;
    const char *fault_description = result->output.fault_description ? result->output.fault_description : "Action Failed";
    log_warn("[soap] action fault service=%s action=%s handler=%s code=%d desc=%s\n",
             service_name, action_name,
             result->handler_name ? result->handler_name : "(none)",
             fault_code, fault_description);
    log_fault_request_detail("handler_fault",
                             method,
                             path,
                             soap_action_header,
                             request,
                             request_len,
                             body,
                             body_len);
    bool built = build_soap_fault(fault_code,
                                  fault_description,
                                  response,
                                  response_size,
                                  response_len);
    if (!built)
    {
        log_error("[soap] failed to build fault response for %s#%s\n",
                  service_name, action_name);
        free(result);
        return false;
    }

    log_debug("[soap] send packet endpoint=%s status=500 bytes=%zu\n",
              path, *response_len);
    log_warn("[soap] assert_http_500 service=%s action=%s handler=%s code=%d\n",
             service_name, action_name,
             result->handler_name ? result->handler_name : "(none)",
             fault_code);
    free(result);
    return true;
}
