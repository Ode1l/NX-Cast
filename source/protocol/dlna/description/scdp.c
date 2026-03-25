#include "scdp.h"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log/log.h"

typedef struct
{
    const char *path;
    const char *body;
    size_t body_len;
} XmlResource;

static const char g_deviceXml[] =
    "<?xml version=\"1.0\"?>\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
    "  <specVersion>\n"
    "    <major>1</major>\n"
    "    <minor>0</minor>\n"
    "  </specVersion>\n"
    "  <device>\n"
    "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\n"
    "    <friendlyName>NX-Cast</friendlyName>\n"
    "    <manufacturer>Ode1l</manufacturer>\n"
    "    <modelName>NX-Cast Virtual Renderer</modelName>\n"
    "    <UDN>uuid:6b0d3c60-3d96-41f4-986c-0a4bb12b0001</UDN>\n"
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
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType>\n"
    "      <allowedValueList><allowedValue>STOPPED</allowedValue><allowedValue>PLAYING</allowedValue><allowedValue>PAUSED_PLAYBACK</allowedValue><allowedValue>TRANSITIONING</allowedValue><allowedValue>NO_MEDIA_PRESENT</allowedValue></allowedValueList>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType><defaultValue>OK</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType><defaultValue>1</defaultValue></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\n"
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
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</dataType><allowedValueList><allowedValue>Master</allowedValue></allowedValueList></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType><defaultValue>20</defaultValue></stateVariable>\n"
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
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>CurrentConnectionIDs</name><dataType>string</dataType><defaultValue>0</defaultValue></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

static const XmlResource g_resources[] = {
    {"/device.xml", g_deviceXml, sizeof(g_deviceXml) - 1},
    {"/scpd/AVTransport.xml", g_avTransportScpd, sizeof(g_avTransportScpd) - 1},
    {"/scpd/RenderingControl.xml", g_renderingControlScpd, sizeof(g_renderingControlScpd) - 1},
    {"/scpd/ConnectionManager.xml", g_connectionManagerScpd, sizeof(g_connectionManagerScpd) - 1},
};

static int g_listenSock = -1;
static Thread g_httpThread;
static bool g_running = false;
static bool g_threadStarted = false;

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
    char buffer[1024];
    ssize_t n = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0)
        return;
    buffer[n] = '\0';

    if (strncmp(buffer, "GET ", 4) != 0)
    {
        send_not_found(clientSock);
        return;
    }

    const char *path_start = buffer + 4;
    const char *path_end = strchr(path_start, ' ');
    if (!path_end)
    {
        send_not_found(clientSock);
        return;
    }

    char path[256];
    size_t path_len = (size_t)(path_end - path_start);
    if (path_len == 0 || path_len >= sizeof(path))
    {
        send_not_found(clientSock);
        return;
    }
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';

    const XmlResource *resource = find_resource(path);
    if (!resource)
    {
        log_warn("[dlna-desc] Unknown request path: %s\n", path);
        send_not_found(clientSock);
        return;
    }

    send_xml(clientSock, resource);
    log_info("[dlna-desc] Served %s\n", path);
}

static void scdp_thread(void *arg)
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

bool scdp_start(uint16_t port)
{
    if (g_running)
        return true;

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
    Result rc = threadCreate(&g_httpThread, scdp_thread, NULL, NULL, 0x4000, 0x2B, -2);
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
    log_info("[dlna-desc] SCDP server listening on :%u\n", port);
    return true;
}

void scdp_stop(void)
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

    log_info("[dlna-desc] SCDP server stopped.\n");
}
