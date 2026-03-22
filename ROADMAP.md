# NX-Cast Roadmap

## Milestone 0
Project bootstrap

- Repository setup
- Basic app framework
- Switch homebrew launch
- Network initialization

## Milestone 1
Device discovery

- `[MVP]` Ship `_airplay._tcp.local` mDNS probe so controllers can locate NX-Cast without Linux.
- `[MVP]` Add an SSDP responder that listens on 239.255.255.250:1900, parses `M-SEARCH` (MAN/ST/MX) and returns `HTTP/1.1 200 OK` with `CACHE-CONTROL/LOCATION/ST/USN`.
- `[Optional]` Emit `NOTIFY ssdp:alive/byebye` on startup/interval/exit to advertise presence proactively.

## Milestone 2
DLNA receiver

- `[MVP]` Serve `/device.xml` describing the MediaRenderer profile and exposing AVTransport/ConnectionManager/RenderingControl services.
- `[MVP]` Implement minimal SCPD + SOAP handlers (`SetAVTransportURI`, `Play`) so controllers can push URLs and initiate playback.
- `[MVP]` Add an HTTP client pipeline (H264 software path initially) that pulls the provided media URL and feeds the renderer.
- `[Optional]` Add eventing (`SUBSCRIBE/NOTIFY`) and richer actions once the core DMR loop is stable.

## Milestone 3
AirPlay receiver

- mDNS discovery
- RTSP session
- Video streaming

## Milestone 4
Performance optimization

- Hardware decode
- Reduced latency

## Milestone 5
User interface

- Settings
- Device name
- Protocol toggles
