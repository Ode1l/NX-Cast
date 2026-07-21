#include "../handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../handler_internal.h"
#include "app/protocol_coordinator.h"
#include "player/renderer.h"

#define RENDERINGCONTROL_OWNER_TOKEN 1u
#define RENDERINGCONTROL_COMMAND_TIMEOUT_MS 750u

static bool renderingcontrol_submit(PlayerCommandKind kind, int value,
                                    bool flag)
{
    ProtocolMediaGuard guard;
    ProtocolMediaTransaction transaction;
    PlayerOwnershipLease lease = {0};
    PlayerCommandRequest request;
    PlayerCommandStatus status;
    bool claimed = false;

    memset(&transaction, 0, sizeof(transaction));
    if (!protocol_coordinator_media_unowned_or_owner_guard_begin(
            PLAYER_MEDIA_OWNER_DLNA, RENDERINGCONTROL_OWNER_TOKEN, &guard))
        return false;
    lease = guard.lease;
    protocol_coordinator_media_guard_end(&guard);

    if (lease.owner == PLAYER_MEDIA_OWNER_NONE)
    {
        if (!protocol_coordinator_media_begin(PLAYER_MEDIA_OWNER_DLNA,
                                              RENDERINGCONTROL_OWNER_TOKEN,
                                              &transaction))
            return false;
        lease = transaction.lease;
        claimed = true;
    }

    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.source = PLAYER_COMMAND_SOURCE_DLNA;
    request.lease = lease;
    request.value = value;
    request.flag = flag;
    status = player_submit_command_wait(
        &request, RENDERINGCONTROL_COMMAND_TIMEOUT_MS);
    if (claimed)
    {
        if (player_command_status_succeeded(status))
            protocol_coordinator_media_end(&transaction);
        else
            protocol_coordinator_media_abort(&transaction);
    }
    return player_command_status_succeeded(status);
}

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

    bool changed = renderingcontrol_submit(PLAYER_COMMAND_SET_VOLUME,
                                           (int)vol, false);
    if (!changed)
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

    bool changed = renderingcontrol_submit(PLAYER_COMMAND_SET_VOLUME,
                                           PLAYER_DEFAULT_VOLUME, false) &&
                   renderingcontrol_submit(PLAYER_COMMAND_SET_MUTE, 0,
                                           false);
    if (!changed)
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

    bool changed = renderingcontrol_submit(PLAYER_COMMAND_SET_MUTE, 0,
                                           strcmp(desired, "0") != 0);
    if (!changed)
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
