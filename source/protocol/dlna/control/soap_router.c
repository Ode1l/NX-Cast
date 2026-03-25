#include "soap_router.h"

#include <string.h>

static const char g_serviceTypeAvTransport[] = "urn:schemas-upnp-org:service:AVTransport:1";
static const char g_serviceTypeRenderingControl[] = "urn:schemas-upnp-org:service:RenderingControl:1";
static const char g_serviceTypeConnectionManager[] = "urn:schemas-upnp-org:service:ConnectionManager:1";

typedef struct
{
    const char *service_name;
    const char *service_type;
    const char *action_name;
    SoapActionHandler handler;
} SoapRouteEntry;

static const SoapRouteEntry g_routes[] = {
    {"AVTransport", g_serviceTypeAvTransport, "SetAVTransportURI", avtransport_set_uri},
    {"AVTransport", g_serviceTypeAvTransport, "Play", avtransport_play},
    {"AVTransport", g_serviceTypeAvTransport, "Pause", avtransport_pause},
    {"AVTransport", g_serviceTypeAvTransport, "Stop", avtransport_stop},
    {"AVTransport", g_serviceTypeAvTransport, "GetTransportInfo", avtransport_get_transport_info},

    {"RenderingControl", g_serviceTypeRenderingControl, "GetVolume", renderingcontrol_get_volume},
    {"RenderingControl", g_serviceTypeRenderingControl, "SetVolume", renderingcontrol_set_volume},

    {"ConnectionManager", g_serviceTypeConnectionManager, "GetProtocolInfo", connectionmanager_get_protocol_info},
    {"ConnectionManager", g_serviceTypeConnectionManager, "GetCurrentConnectionIDs", connectionmanager_get_current_connection_ids}
};

const char *soap_router_service_type_from_name(const char *service_name)
{
    if (!service_name)
        return NULL;
    if (strcmp(service_name, "AVTransport") == 0)
        return g_serviceTypeAvTransport;
    if (strcmp(service_name, "RenderingControl") == 0)
        return g_serviceTypeRenderingControl;
    if (strcmp(service_name, "ConnectionManager") == 0)
        return g_serviceTypeConnectionManager;
    return NULL;
}

bool soap_router_route_action(const SoapActionContext *ctx, SoapRouteResult *result)
{
    if (!ctx || !result)
        return false;

    memset(result, 0, sizeof(*result));

    size_t route_count = sizeof(g_routes) / sizeof(g_routes[0]);
    for (size_t i = 0; i < route_count; ++i)
    {
        if (strcmp(ctx->service_name, g_routes[i].service_name) != 0)
            continue;
        if (strcmp(ctx->action_name, g_routes[i].action_name) != 0)
            continue;

        result->service_type = g_routes[i].service_type;
        result->action_name = g_routes[i].action_name;
        return g_routes[i].handler(ctx, &result->output);
    }

    result->service_type = soap_router_service_type_from_name(ctx->service_name);
    result->action_name = ctx->action_name;
    result->output.success = false;
    result->output.fault_code = 401;
    result->output.fault_description = "Invalid Action";
    result->output.output_xml[0] = '\0';
    return false;
}
