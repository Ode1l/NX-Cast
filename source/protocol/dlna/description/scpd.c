#include "scpd.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

typedef struct
{
    const char *path;
    const char *body;
    size_t body_len;
} XmlResource;

#define DEVICE_XML_BUFFER_SIZE 4096

static const char g_deviceXmlTemplate[] =
    "<?xml version=\"1.0\"?>\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
    "  <specVersion>\n"
    "    <major>1</major>\n"
    "    <minor>0</minor>\n"
    "  </specVersion>\n"
    "  <device>\n"
    "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\n"
    "    <friendlyName>%s</friendlyName>\n"
    "    <manufacturer>%s</manufacturer>\n"
    "    <modelName>%s</modelName>\n"
    "    <UDN>%s</UDN>\n"
    "    <serviceList>\n"
    "      <service>\n"
    "        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\n"
    "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\n"
    "        <SCPDURL>/scpd/AVTransport.xml</SCPDURL>\n"
    "        <controlURL>/upnp/control/AVTransport</controlURL>\n"
    "        <eventSubURL>/upnp/event/AVTransport</eventSubURL>\n"
    "      </service>\n"
    "      <service>\n"
    "        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>\n"
    "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\n"
    "        <SCPDURL>/scpd/RenderingControl.xml</SCPDURL>\n"
    "        <controlURL>/upnp/control/RenderingControl</controlURL>\n"
    "        <eventSubURL>/upnp/event/RenderingControl</eventSubURL>\n"
    "      </service>\n"
    "      <service>\n"
    "        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>\n"
    "        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\n"
    "        <SCPDURL>/scpd/ConnectionManager.xml</SCPDURL>\n"
    "        <controlURL>/upnp/control/ConnectionManager</controlURL>\n"
    "        <eventSubURL>/upnp/event/ConnectionManager</eventSubURL>\n"
    "      </service>\n"
    "    </serviceList>\n"
    "  </device>\n"
    "</root>\n";

static const char g_defaultFriendlyName[] = "NX-Cast";
static const char g_defaultManufacturer[] = "Ode1l";
static const char g_defaultModelName[] = "NX-Cast Virtual Renderer";
static const char g_defaultUuid[] = "uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001";
static char g_deviceXmlBuffer[DEVICE_XML_BUFFER_SIZE];

static const char g_avTransportScpd[] =
    "<?xml version=\"1.0\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action>\n"
    "      <name>SetAVTransportURI</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>Play</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Speed</name><direction>in</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>Pause</name><argumentList><argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument></argumentList></action>\n"
    "    <action><name>Stop</name><argumentList><argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument></argumentList></action>\n"
    "    <action>\n"
    "      <name>GetTransportInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentTransportState</name><direction>out</direction><relatedStateVariable>TransportState</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentTransportStatus</name><direction>out</direction><relatedStateVariable>TransportStatus</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentSpeed</name><direction>out</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetCurrentTransportActions</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Actions</name><direction>out</direction><relatedStateVariable>CurrentTransportActions</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetMediaInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>NrTracks</name><direction>out</direction><relatedStateVariable>NumberOfTracks</relatedStateVariable></argument>\n"
    "        <argument><name>MediaDuration</name><direction>out</direction><relatedStateVariable>CurrentMediaDuration</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentURI</name><direction>out</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentURIMetaData</name><direction>out</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\n"
    "        <argument><name>NextURI</name><direction>out</direction><relatedStateVariable>NextAVTransportURI</relatedStateVariable></argument>\n"
    "        <argument><name>NextURIMetaData</name><direction>out</direction><relatedStateVariable>NextAVTransportURIMetaData</relatedStateVariable></argument>\n"
    "        <argument><name>PlayMedium</name><direction>out</direction><relatedStateVariable>PlaybackStorageMedium</relatedStateVariable></argument>\n"
    "        <argument><name>RecordMedium</name><direction>out</direction><relatedStateVariable>RecordStorageMedium</relatedStateVariable></argument>\n"
    "        <argument><name>WriteStatus</name><direction>out</direction><relatedStateVariable>RecordMediumWriteStatus</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetPositionInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Track</name><direction>out</direction><relatedStateVariable>CurrentTrack</relatedStateVariable></argument>\n"
    "        <argument><name>TrackDuration</name><direction>out</direction><relatedStateVariable>CurrentTrackDuration</relatedStateVariable></argument>\n"
    "        <argument><name>TrackMetaData</name><direction>out</direction><relatedStateVariable>CurrentTrackMetaData</relatedStateVariable></argument>\n"
    "        <argument><name>TrackURI</name><direction>out</direction><relatedStateVariable>CurrentTrackURI</relatedStateVariable></argument>\n"
    "        <argument><name>RelTime</name><direction>out</direction><relatedStateVariable>RelativeTimePosition</relatedStateVariable></argument>\n"
    "        <argument><name>AbsTime</name><direction>out</direction><relatedStateVariable>AbsoluteTimePosition</relatedStateVariable></argument>\n"
    "        <argument><name>RelCount</name><direction>out</direction><relatedStateVariable>RelativeCounterPosition</relatedStateVariable></argument>\n"
    "        <argument><name>AbsCount</name><direction>out</direction><relatedStateVariable>AbsoluteCounterPosition</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>Seek</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Unit</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SeekMode</relatedStateVariable></argument>\n"
    "        <argument><name>Target</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SeekTarget</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SeekMode</name><dataType>string</dataType><allowedValueList><allowedValue>REL_TIME</allowedValue></allowedValueList></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SeekTarget</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>LastChange</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType>\n"
    "      <allowedValueList><allowedValue>STOPPED</allowedValue><allowedValue>PLAYING</allowedValue><allowedValue>PAUSED_PLAYBACK</allowedValue><allowedValue>TRANSITIONING</allowedValue><allowedValue>NO_MEDIA_PRESENT</allowedValue></allowedValueList>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType><defaultValue>OK</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType><defaultValue>1</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTransportActions</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NumberOfTracks</name><dataType>ui4</dataType><defaultValue>1</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrack</name><dataType>ui4</dataType><defaultValue>1</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentMediaDuration</name><dataType>string</dataType><defaultValue>00:00:00</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackDuration</name><dataType>string</dataType><defaultValue>00:00:00</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentTrackURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RelativeTimePosition</name><dataType>string</dataType><defaultValue>00:00:00</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteTimePosition</name><dataType>string</dataType><defaultValue>00:00:00</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RelativeCounterPosition</name><dataType>i4</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>AbsoluteCounterPosition</name><dataType>i4</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>NextAVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>PlaybackStorageMedium</name><dataType>string</dataType><defaultValue>NETWORK</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordStorageMedium</name><dataType>string</dataType><defaultValue>NOT_IMPLEMENTED</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>RecordMediumWriteStatus</name><dataType>string</dataType><defaultValue>NOT_IMPLEMENTED</defaultValue></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static const char g_renderingControlScpd[] =
    "<?xml version=\"1.0\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action>\n"
    "      <name>GetVolume</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentVolume</name><direction>out</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>SetVolume</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\n"
    "        <argument><name>DesiredVolume</name><direction>in</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetMute</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\n"
    "        <argument><name>CurrentMute</name><direction>out</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>SetMute</name>\n"
    "      <argumentList>\n"
    "        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\n"
    "        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\n"
    "        <argument><name>DesiredMute</name><direction>in</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</dataType><allowedValueList><allowedValue>Master</allowedValue></allowedValueList></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>LastChange</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType><defaultValue>20</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>Mute</name><dataType>boolean</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static const char g_connectionManagerScpd[] =
    "<?xml version=\"1.0\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action>\n"
    "      <name>GetProtocolInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>\n"
    "        <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetCurrentConnectionIDs</name>\n"
    "      <argumentList>\n"
    "        <argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action>\n"
    "      <name>GetCurrentConnectionInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\n"
    "        <argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>\n"
    "        <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>\n"
    "        <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>\n"
    "        <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>\n"
    "        <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\n"
    "        <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>\n"
    "        <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType><allowedValueList><allowedValue>Input</allowedValue><allowedValue>Output</allowedValue></allowedValueList></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType><allowedValueList><allowedValue>OK</allowedValue><allowedValue>ContentFormatMismatch</allowedValue><allowedValue>InsufficientBandwidth</allowedValue><allowedValue>UnreliableChannel</allowedValue><allowedValue>Unknown</allowedValue></allowedValueList></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentConnectionIDs</name><dataType>string</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static XmlResource g_resources[] = {
    {"/device.xml", NULL, 0},
    {"/scpd/AVTransport.xml", g_avTransportScpd, sizeof(g_avTransportScpd) - 1},
    {"/scpd/RenderingControl.xml", g_renderingControlScpd, sizeof(g_renderingControlScpd) - 1},
    {"/scpd/ConnectionManager.xml", g_connectionManagerScpd, sizeof(g_connectionManagerScpd) - 1},
};

static bool g_running = false;

static const char *coalesce_string(const char *value, const char *fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return value;
}

static bool build_device_xml(const ScpdConfig *config)
{
    const char *friendly_name = g_defaultFriendlyName;
    const char *manufacturer = g_defaultManufacturer;
    const char *model_name = g_defaultModelName;
    const char *uuid = g_defaultUuid;

    if (config)
    {
        friendly_name = coalesce_string(config->friendly_name, g_defaultFriendlyName);
        manufacturer = coalesce_string(config->manufacturer, g_defaultManufacturer);
        model_name = coalesce_string(config->model_name, g_defaultModelName);
        uuid = coalesce_string(config->uuid, g_defaultUuid);
    }

    int written = snprintf(g_deviceXmlBuffer, sizeof(g_deviceXmlBuffer), g_deviceXmlTemplate,
                           friendly_name, manufacturer, model_name, uuid);
    if (written < 0 || (size_t)written >= sizeof(g_deviceXmlBuffer))
    {
        log_error("[scpd] device.xml generation failed, buffer too small.\n");
        return false;
    }

    g_resources[0].body = g_deviceXmlBuffer;
    g_resources[0].body_len = (size_t)written;
    return true;
}

static const XmlResource *find_resource(const char *path)
{
    size_t resource_count = sizeof(g_resources) / sizeof(g_resources[0]);
    for (size_t i = 0; i < resource_count; ++i)
    {
        if (strcmp(path, g_resources[i].path) == 0)
            return &g_resources[i];
    }
    return NULL;
}

static bool build_http_response(int status,
                                const char *status_text,
                                const char *content_type,
                                const char *body,
                                size_t body_len,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    if (!status_text || !content_type || !body || !response || response_size == 0 || !response_len)
        return false;

    int written = snprintf(response, response_size,
                           "HTTP/1.1 %d %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%.*s",
                           status,
                           status_text,
                           content_type,
                           body_len,
                           (int)body_len,
                           body);

    if (written < 0 || (size_t)written >= response_size)
        return false;

    *response_len = (size_t)written;
    return true;
}

static bool build_text_response(int status,
                                const char *status_text,
                                const char *body,
                                char *response,
                                size_t response_size,
                                size_t *response_len)
{
    return build_http_response(status,
                               status_text,
                               "text/plain; charset=\"utf-8\"",
                               body,
                               strlen(body),
                               response,
                               response_size,
                               response_len);
}

bool scpd_start(const ScpdConfig *config)
{
    if (g_running)
        return true;

    if (!build_device_xml(config))
        return false;

    g_running = true;
    log_info("[scpd] description resources ready.\n");
    return true;
}

void scpd_stop(void)
{
    if (!g_running)
        return;

    g_running = false;
    log_info("[scpd] description resources stopped.\n");
}

bool scpd_try_handle_http(const char *method,
                          const char *path,
                          char *response,
                          size_t response_size,
                          size_t *response_len)
{
    if (!path || !response || !response_len)
        return false;

    *response_len = 0;

    bool is_device_desc = strcmp(path, "/device.xml") == 0;
    bool is_service_desc = strncmp(path, "/scpd/", strlen("/scpd/")) == 0;
    if (!is_device_desc && !is_service_desc)
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

    if (!method || strcmp(method, "GET") != 0)
    {
        return build_text_response(405,
                                   "Method Not Allowed",
                                   "SCPD endpoint requires GET",
                                   response,
                                   response_size,
                                   response_len);
    }

    const XmlResource *resource = find_resource(path);
    if (!resource || !resource->body)
    {
        log_warn("[scpd] unknown path=%s\n", path);
        return build_text_response(404,
                                   "Not Found",
                                   "Not Found",
                                   response,
                                   response_size,
                                   response_len);
    }

    bool built = build_http_response(200,
                                     "OK",
                                     "application/xml; charset=\"utf-8\"",
                                     resource->body,
                                     resource->body_len,
                                     response,
                                     response_size,
                                     response_len);
    if (!built)
    {
        log_error("[scpd] failed to build response for path=%s\n", path);
        return false;
    }

    log_info("[scpd] served path=%s send_bytes=%zu\n", path, *response_len);
    return true;
}
