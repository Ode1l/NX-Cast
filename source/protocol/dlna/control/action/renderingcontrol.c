#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "../handler_internal.h"

bool renderingcontrol_get_volume(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char channel[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Channel", channel, sizeof(channel)))
        return false;

    char response[SOAP_HANDLER_OUTPUT_MAX];
    int len = snprintf(response, sizeof(response),
                       "<CurrentVolume>%d</CurrentVolume>",
                       g_soap_runtime_state.volume);
    if (len < 0 || (size_t)len >= sizeof(response))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, response);
    return true;
}

bool renderingcontrol_set_volume(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char channel[32];
    char desired[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Channel", channel, sizeof(channel)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "DesiredVolume", desired, sizeof(desired)))
        return false;

    long vol = strtol(desired, NULL, 10);
    if (vol < 0)
        vol = 0;
    if (vol > 100)
        vol = 100;

    g_soap_runtime_state.volume = (int)vol;
    soap_handler_set_success(out, "");
    return true;
}
