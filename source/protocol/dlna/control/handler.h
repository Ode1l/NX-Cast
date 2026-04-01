#pragma once

#include <stdbool.h>

#define SOAP_HANDLER_OUTPUT_MAX 4096

typedef struct
{
    const char *service_name;
    const char *action_name;
    const char *body;
} SoapActionContext;

typedef struct
{
    bool success;
    int fault_code;
    const char *fault_description;
    char output_xml[SOAP_HANDLER_OUTPUT_MAX];
} SoapActionOutput;

typedef bool (*SoapActionHandler)(const SoapActionContext *ctx, SoapActionOutput *out);

void soap_handler_init(void);
void soap_handler_shutdown(void);

bool avtransport_set_uri(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_play(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_pause(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_stop(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_get_transport_info(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_get_current_transport_actions(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_get_media_info(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_get_position_info(const SoapActionContext *ctx, SoapActionOutput *out);
bool avtransport_seek(const SoapActionContext *ctx, SoapActionOutput *out);

bool renderingcontrol_get_volume(const SoapActionContext *ctx, SoapActionOutput *out);
bool renderingcontrol_set_volume(const SoapActionContext *ctx, SoapActionOutput *out);
bool renderingcontrol_get_mute(const SoapActionContext *ctx, SoapActionOutput *out);
bool renderingcontrol_set_mute(const SoapActionContext *ctx, SoapActionOutput *out);

bool connectionmanager_get_protocol_info(const SoapActionContext *ctx, SoapActionOutput *out);
bool connectionmanager_get_current_connection_ids(const SoapActionContext *ctx, SoapActionOutput *out);
bool connectionmanager_get_current_connection_info(const SoapActionContext *ctx, SoapActionOutput *out);
