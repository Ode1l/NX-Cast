#include "scpd.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"
#include "protocol/dlna/control/soap_server.h"

typedef struct
{
    const char *path;
    const char *body;
    size_t body_len;
} XmlResource;

#define DEVICE_XML_BUFFER_SIZE 4096
// Request/response buffers are intentionally not placed on thread stack.
// We previously hit crashes due to stack pressure in the SCPD worker thread.
#define SCPD_REQUEST_BUFFER_SIZE 16384
#define SCPD_SOAP_RESPONSE_BUFFER_SIZE 8192
// Keep this above the old 0x4000 default to avoid stack overflow on libnx threads.
#define SCPD_THREAD_STACK_SIZE 0x8000

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
    "    <stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType>\n"
    "      <allowedValueList><allowedValue>STOPPED</allowedValue><allowedValue>PLAYING</allowedValue><allowedValue>PAUSED_PLAYBACK</allowedValue><allowedValue>TRANSITIONING</allowedValue><allowedValue>NO_MEDIA_PRESENT</allowedValue></allowedValueList>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType><defaultValue>OK</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType><defaultValue>1</defaultValue></stateVariable>\n"
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

static int g_listenSock = -1;
static Thread g_httpThread;
static bool g_running = false;
static bool g_threadStarted = false;

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
        log_error("[dlna-desc] device.xml generation failed, buffer too small.\n");
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

static void send_not_found(int clientSock)
{
    const char *response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Not Found";
    send(clientSock, response, strlen(response), 0);
}

static void send_xml(int clientSock, const XmlResource *resource)
{
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/xml; charset=\"utf-8\"\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              resource->body_len);
    if (header_len > 0)
        send(clientSock, header, (size_t)header_len, 0);
    send(clientSock, resource->body, resource->body_len, 0);
}

static void handle_client(int clientSock)
{
    // Allocate large I/O buffers on heap to keep worker-thread stack small/stable.
    char *buffer = malloc(SCPD_REQUEST_BUFFER_SIZE);
    char *soap_response = malloc(SCPD_SOAP_RESPONSE_BUFFER_SIZE);
    if (!buffer || !soap_response)
    {
        log_error("[dlna-desc] OOM while handling HTTP request.\n");
        free(buffer);
        free(soap_response);
        send_not_found(clientSock);
        return;
    }

    ssize_t n = recv(clientSock, buffer, SCPD_REQUEST_BUFFER_SIZE - 1, 0);
    if (n <= 0)
    {
        free(buffer);
        free(soap_response);
        return;
    }
    buffer[n] = '\0';
    log_debug("[dlna-desc] HTTP recv bytes=%zd\n", n);

    char method[8];
    char raw_path[256];
    if (sscanf(buffer, "%7s %255s", method, raw_path) != 2)
    {
        log_debug("[dlna-desc] HTTP parse failed (request line).\n");
        send_not_found(clientSock);
        free(buffer);
        free(soap_response);
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s", raw_path);
    char *query = strchr(path, '?');
    if (query)
        *query = '\0';
    log_debug("[dlna-desc] HTTP request %s %s\n", method, path);

    size_t soap_response_len = 0;
    if (soap_server_try_handle_http(method, path, buffer, (size_t)n,
                             soap_response, SCPD_SOAP_RESPONSE_BUFFER_SIZE, &soap_response_len))
    {
        if (soap_response_len > 0)
            send(clientSock, soap_response, soap_response_len, 0);
        log_info("[dlna-desc] SOAP handled %s %s\n", method, path);
        free(buffer);
        free(soap_response);
        return;
    }

    if (strcmp(method, "GET") != 0)
    {
        send_not_found(clientSock);
        free(buffer);
        free(soap_response);
        return;
    }

    const XmlResource *resource = find_resource(path);
    if (!resource || !resource->body)
    {
        log_warn("[dlna-desc] Unknown request path: %s\n", path);
        send_not_found(clientSock);
        free(buffer);
        free(soap_response);
        return;
    }

    send_xml(clientSock, resource);
    log_info("[dlna-desc] Served %s\n", path);
    free(buffer);
    free(soap_response);
}

static void scpd_thread(void *arg)
{
    (void)arg;
    while (g_running)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_listenSock, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(g_listenSock + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            if (errno == EINTR)
                continue;
            log_error("[dlna-desc] select failed: %s (%d)\n", strerror(errno), errno);
            break;
        }
        if (ret == 0)
            continue;

        if (FD_ISSET(g_listenSock, &readfds))
        {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientSock = accept(g_listenSock, (struct sockaddr *)&clientAddr, &clientLen);
            if (clientSock < 0)
                continue;

            handle_client(clientSock);
            close(clientSock);
        }
    }
}

bool scpd_start(uint16_t port, const ScpdConfig *config)
{
    if (g_running)
        return true;

    if (!build_device_xml(config))
        return false;

    g_listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenSock < 0)
    {
        log_error("[dlna-desc] socket failed: %s (%d)\n", strerror(errno), errno);
        return false;
    }

    int reuse = 1;
    setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listenSock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_error("[dlna-desc] bind failed on port %u: %s (%d)\n", port, strerror(errno), errno);
        close(g_listenSock);
        g_listenSock = -1;
        return false;
    }

    if (listen(g_listenSock, 8) < 0)
    {
        log_error("[dlna-desc] listen failed: %s (%d)\n", strerror(errno), errno);
        close(g_listenSock);
        g_listenSock = -1;
        return false;
    }

    g_running = true;
    Result rc = threadCreate(&g_httpThread, scpd_thread, NULL, NULL, SCPD_THREAD_STACK_SIZE, 0x2B, -2);
    if (R_FAILED(rc))
    {
        log_error("[dlna-desc] threadCreate failed: 0x%08X\n", rc);
        g_running = false;
        close(g_listenSock);
        g_listenSock = -1;
        return false;
    }

    rc = threadStart(&g_httpThread);
    if (R_FAILED(rc))
    {
        log_error("[dlna-desc] threadStart failed: 0x%08X\n", rc);
        threadClose(&g_httpThread);
        g_running = false;
        close(g_listenSock);
        g_listenSock = -1;
        return false;
    }

    g_threadStarted = true;
    log_info("[dlna-desc] SCPD server listening on :%u\n", port);
    return true;
}

void scpd_stop(void)
{
    if (!g_running)
        return;

    g_running = false;

    if (g_threadStarted)
    {
        threadWaitForExit(&g_httpThread);
        threadClose(&g_httpThread);
        g_threadStarted = false;
    }

    if (g_listenSock >= 0)
    {
        close(g_listenSock);
        g_listenSock = -1;
    }

    log_info("[dlna-desc] SCPD server stopped.\n");
}
