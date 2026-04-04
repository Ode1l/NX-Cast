#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../handler_internal.h"
#include "player/player.h"

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

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "CurrentVolume", g_soap_runtime_state.volume))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
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

    if (!player_set_volume((int)vol))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, "");
    return true;
}

bool renderingcontrol_get_mute(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char instance_id[32];
    char channel[32];

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg(ctx, out, "InstanceID", instance_id, sizeof(instance_id)))
        return false;

    if (!soap_handler_require_arg(ctx, out, "Channel", channel, sizeof(channel)))
        return false;

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "CurrentMute", g_soap_runtime_state.mute ? 1 : 0))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, NULL);
    return true;
}

bool renderingcontrol_set_mute(const SoapActionContext *ctx, SoapActionOutput *out)
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

    if (!soap_handler_require_arg(ctx, out, "DesiredMute", desired, sizeof(desired)))
        return false;

    if (!player_set_mute(strcmp(desired, "0") != 0))
    {
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    soap_handler_set_success(out, "");
    return true;
}
