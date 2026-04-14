#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../handler_internal.h"
#include "player/renderer.h"

bool renderingcontrol_get_brightness(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "CurrentBrightness", 100))
    {
        free(instance_id);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    soap_handler_set_success(out, NULL);
    return true;
}

bool renderingcontrol_get_volume(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *channel = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "Channel", &channel))
    {
        free(instance_id);
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "CurrentVolume", state->volume))
    {
        free(instance_id);
        free(channel);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    free(channel);
    soap_handler_set_success(out, NULL);
    return true;
}

bool renderingcontrol_set_volume(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *channel = NULL;
    char *desired = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "Channel", &channel))
    {
        free(instance_id);
        return false;
    }

    if (!soap_handler_require_arg_alloc(ctx, out, "DesiredVolume", &desired))
    {
        free(instance_id);
        free(channel);
        return false;
    }

    long vol = strtol(desired, NULL, 10);
    if (vol < 0)
        vol = 0;
    if (vol > 100)
        vol = 100;

    if (!renderer_set_volume((int)vol))
    {
        free(instance_id);
        free(channel);
        free(desired);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    free(channel);
    free(desired);
    soap_handler_set_success(out, "");
    return true;
}

bool renderingcontrol_list_presets(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    soap_writer_clear(out);
    if (!soap_writer_element_text(out, "CurrentPresetNameList", "FactoryDefaults"))
    {
        free(instance_id);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    soap_handler_set_success(out, NULL);
    return true;
}

bool renderingcontrol_select_preset(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *preset_name = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "PresetName", &preset_name))
    {
        free(instance_id);
        return false;
    }

    if (strcasecmp(preset_name, "FactoryDefaults") != 0)
    {
        free(instance_id);
        free(preset_name);
        soap_handler_set_fault(out, 402, "Invalid Args");
        return false;
    }

    if (!renderer_set_volume(PLAYER_DEFAULT_VOLUME) || !renderer_set_mute(false))
    {
        free(instance_id);
        free(preset_name);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    free(preset_name);
    soap_handler_set_success(out, "");
    return true;
}

bool renderingcontrol_get_mute(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *channel = NULL;
    const DlnaProtocolStateView *state = dlna_protocol_state_view();

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "Channel", &channel))
    {
        free(instance_id);
        return false;
    }

    soap_writer_clear(out);
    if (!soap_writer_element_int(out, "CurrentMute", state->mute ? 1 : 0))
    {
        free(instance_id);
        free(channel);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    free(channel);
    soap_handler_set_success(out, NULL);
    return true;
}

bool renderingcontrol_set_mute(const SoapActionContext *ctx, SoapActionOutput *out)
{
    char *instance_id = NULL;
    char *channel = NULL;
    char *desired = NULL;

    if (!ctx || !out)
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "InstanceID", &instance_id))
        return false;

    if (!soap_handler_require_arg_alloc(ctx, out, "Channel", &channel))
    {
        free(instance_id);
        return false;
    }

    if (!soap_handler_require_arg_alloc(ctx, out, "DesiredMute", &desired))
    {
        free(instance_id);
        free(channel);
        return false;
    }

    if (!renderer_set_mute(strcmp(desired, "0") != 0))
    {
        free(instance_id);
        free(channel);
        free(desired);
        soap_handler_set_fault(out, 501, "Action Failed");
        return false;
    }

    free(instance_id);
    free(channel);
    free(desired);
    soap_handler_set_success(out, "");
    return true;
}
