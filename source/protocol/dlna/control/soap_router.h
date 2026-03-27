#pragma once

#include <stdbool.h>

#include "handler.h"

typedef struct
{
    const char *service_type;
    const char *action_name;
    const char *handler_name;
    SoapActionOutput output;
} SoapRouteResult;

bool soap_router_route_action(const SoapActionContext *ctx, SoapRouteResult *result);

const char *soap_router_service_type_from_name(const char *service_name);
