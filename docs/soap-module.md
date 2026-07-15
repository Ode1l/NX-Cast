# SOAP And Eventing Module

This document describes the current boundary of `source/protocol/dlna/control/`.

## Directory

```text
source/protocol/dlna/control/
  action/
  event_server.*
  handler.*
  protocol_state.*
  soap_router.*
  soap_server.*
  soap_writer.*
```

## Responsibilities

### soap_server

Responsible for:

1. HTTP entry handling.
2. SOAP envelope and fault wrapping.
3. Forwarding requests to the router.

### soap_router

Responsible for routing by `service + action`.

### soap_writer

Responsible for:

1. Dynamic XML output.
2. Shared XML escaping.

### handler

Responsible for:

1. Argument extraction.
2. XML entity decoding.
3. Bridging SOAP actions to the player.
4. Synchronizing player events into protocol state.

### protocol_state

Responsible for:

1. Owning the single protocol-observed state.
2. Serving SOAP query reads.
3. Serving `LastChange` generation.
4. Serving compatibility helper reads.

This is one of the most important current structure decisions.

### event_server

Responsible for:

1. `SUBSCRIBE / UNSUBSCRIBE`.
2. `NOTIFY`.
3. `LastChange`.

## Control Flow

### SOAP

```text
HTTP POST
  -> soap_server
  -> soap_router
  -> action
  -> player / protocol_state
  -> soap_writer
  -> HTTP response
```

### Eventing

```text
SUBSCRIBE / UNSUBSCRIBE
  -> event_server

player event
  -> handler
  -> protocol_state
  -> event_server worker
  -> NOTIFY / LastChange
```

## Rules

1. Protocol code should do protocol work only.
2. Protocol code should not become a source-specific adapter layer.
3. Protocol state has one owner.
4. SOAP queries and event notifications share the same state.
5. The player remains the source of real playback state.

## Implemented Actions

### AVTransport

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`

### RenderingControl

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness` compatibility stub

### ConnectionManager

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## Avoided Old Patterns

The current code avoids:

1. Per-action private runtime state.
2. A separate `LastChange` state model.
3. Fixed-size manual XML buffer assembly.
4. Source-specific behavior inside SOAP.

## Current Focus

1. Keep protocol-observed state consistent.
2. Improve controller compatibility.
3. Keep query, event, and action semantics aligned.

## Related Documents

1. [dmr-implementation.md](dmr-implementation.md)
2. [scpd-module.md](scpd-module.md)
3. [source-compatibility.md](source-compatibility.md)
