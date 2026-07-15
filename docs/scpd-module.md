# SCPD And Description Module

This document describes the current boundary of `source/protocol/dlna/description/`.

## Responsibility

The description layer serves:

1. `Description.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`
5. `SinkProtocolInfo.csv`

It does not handle:

1. Discovery.
2. Playback.
3. Action execution.
4. Runtime state ownership.

In short, this layer exposes current device capabilities through maintainable template resources.

## Implementation

Description XML is not hard-coded as large C strings.

The current model is:

1. Default templates live in `assets/dlna/`.
2. Switch runtime reads from `sdmc:/switch/NX-Cast/dlna/`.
3. Requests stream template resources.
4. Runtime device values and URLs are substituted while serving the response.

Runtime assets:

1. `Description.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`
5. `SinkProtocolInfo.csv`

## Device Description

The device description declares:

1. `MediaRenderer:1`
2. `AVTransport`
3. `RenderingControl`
4. `ConnectionManager`
5. Each service `SCPDURL`, `controlURL`, and `eventSubURL`

## Service Description

### AVTransport

Declared actions:

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`
7. `LastChange`

### RenderingControl

Declared actions:

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness`
4. `LastChange`

### ConnectionManager

Declared actions:

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## Rules

1. Capability descriptions should be as complete as the current implementation can support.
2. Descriptions must stay aligned with real runtime behavior.
3. Templates should stay simple and editable.
4. Dynamic values should be substituted only at response time.

## SinkProtocolInfo

`SinkProtocolInfo` comes from a CSV template resource.

The goal is not to pretend to be an all-capable renderer. The goal is to declare the current real capability surface and expand it as playback compatibility improves.

## Related Documents

1. [dmr-implementation.md](dmr-implementation.md)
2. [player-layer.md](player-layer.md)
