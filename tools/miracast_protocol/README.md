# Offline Miracast Control Prototype

This is **not a network receiver**. It implements the M1/M2 OPTIONS exchange
and a restricted offline M3/M4 parameter negotiation. It does not open sockets,
advertise a device or play media. It is excluded from the application build and
release archive.

## Run

From the repository root, with a host C compiler and make:

```sh
make -C tools/miracast_protocol test
make -C tools/miracast_protocol sanitize
```

The second target needs compiler ASan/UBSan support. macOS execution is verified;
Windows/MSYS execution is not yet verified. Outputs go to
`build/miracast-protocol/`; the app NRO/ELF are not replaced.

## Contract

- One owner calls a `WfdOptions` instance. There are no threads, callbacks,
  globals, sockets or heap allocations in the handshake core.
- Feed M1 through `source/protocol/rtsp/request.h` first. Only successful parsed
  requests may be passed to `wfd_options_request`; the caller clears their body.
- The shared parser handles partial input; the future transport retains
  unconsumed bytes and enforces bounded receive capacity. The test exercises all
  prefix lengths of a sample M1. No socket fragmentation test is claimed.
- A valid M1 generates two ordered wire messages: its response with the peer's
  CSeq, then M2 using our independent CSeq. Output needs room for a trailing NUL;
  `written` excludes it. Insufficient space leaves both state and output intact.
- The caller queues/copies those bytes before another call and handles short
  writes. On failed writes it closes the exchange. Waiting for M2 is not proof
  that a socket write has succeeded. The timeout includes transmit time.
- `wfd_options_response_bytes` decodes one M2 response before applying it to the
  handshake. It checks RTSP/1.0 framing, status, CSeq, length and field limits;
  duplicate CSeq/Content-Length, transfer encoding and folded headers are
  rejected. Repeated Public list fields are combined in order within one bound.
- The standalone decoder in `response.h` returns a borrowed binary body span,
  valid only while the input buffer remains intact. No body copy or allocation
  occurs. Incomplete/error input leaves its output unchanged and consumes zero.
  Successful decoding consumes one full response including its body; a trailing
  M3 request is left for the request parser, not discarded. The caller routes
  requests and responses separately and must close on framing errors, not scan
  ahead for a new status line. It must tick deadlines even on incomplete input.
- An unrelated CSeq does not advance the exchange or extend its timeout. A
  matching 1xx continues waiting; a final non-200 or missing required method
  fails. Time is caller-provided monotonic milliseconds, with per-phase timeout.
- Request validation errors produce no wire reply and leave the exchange waiting
  until its deadline. A future adapter must map errors to RTSP responses/close;
  this tool does not silently pretend to handle them on a real connection.
- `COMPLETE` means OPTIONS complete, **not** connected media, formats selected,
  buffers ready, or player ownership acquired. New connections use new instances;
  old callbacks must never be dispatched into a replacement instance.

GET_PARAMETER/SET_PARAMETER are implemented only for the laboratory subset
below. SETUP triggers and transport establishment remain unimplemented. Do not
expose this prototype on a network.

## Laboratory M3/M4 Profile

`parameters.h` deliberately has no runtime defaults or automatic device
capability detection. Its initializer requires completed OPTIONS, the source
IPv4 address, and a nonzero RTP port. Tests supply a fictional endpoint/port;
these are not bound sockets. A future runtime must derive the address from the
control peer and bind a usable media port before offering it.

| Parameter | Laboratory offer / accepted selection |
|---|---|
| Video | One H.264 descriptor, profile `01`, level `01`, CEA mask `00000021`; native `28`, preferred mode disabled |
| Resolution | CEA bit 0 (640x480p60) or bit 5 (1280x720p30); M4 must select exactly one |
| Audio | LPCM `00000002` or AAC `00000001` (48 kHz stereo); explicit `none` also accepted in M4 |
| RTP | UDP unicast, supplied sink port, secondary port zero, `mode=play` |
| URL | `rtsp://<control-peer-IPv4>/wfd1.0/streamid=0 none` only |
| Optional capabilities | Content protection, EDID, coupled sink, UIBC, 3D and I2C return `none` when queried |

This fixed profile is a parser/selection fixture, **not a production claim that
these formats have been decoded on Switch**. High profile, other levels,
VESA/HH modes, nonzero slice/frame-rate-control settings, IPv6/alternate URL
forms, secondary sinks and HDCP are outside this subset. Video native/max
dimensions are syntax checked but ignored in M4; latency is syntax checked.
Unsupported profile choices receive 406, not silent fallback.

- M3 replies include only requested known fields. Empty GET_PARAMETER receives
  200 without changing selection; this is not a negotiated keepalive timer.
- Nonempty bodies require `Content-Type: text/parameters`, CRLF-delimited lines,
  no embedded NUL/control bytes, at most 4096 bytes and 16 distinct fields.
- Unknown query/setting fields receive 451; malformed input 400; oversized
  bodies 413; unsupported content type 415. These are conservative laboratory
  error responses, not full WFD parameter-error-body conformance.
- Each SET_PARAMETER is validated into a candidate copy. Only after the entire
  successful reply fits the output buffer is the candidate committed. Invalid
  fields or insufficient output capacity cannot partially update selection.
- Valid partial M4 requests accumulate; `selection.complete` becomes true only
  after video, audio (including explicit `none`), presentation URL and port have
  arrived. It is configuration completeness, never player readiness. Omitted
  audio stays incomplete in this laboratory policy.
- Identical repeated selections are harmless. Updates are allowed only during
  negotiation; the future session owner must stop routing here once SETUP starts.
  This module neither recognizes M5 triggers nor starts/replaces a player.
- Reply serialization is not proof of successful transmission. The future
  transport must copy/queue the response and close the session on send failure.

The test runs M1/M2 to initialize this module, then M3 and full/partial M4 through
the common request parser. It checks atomic rollback, response lengths/CSeq,
repeated selection, unknown fields, format/port/URL rejection and output limits.

## Evidence And Limits

The exchange order and WFD extension tag were checked against
[MiracleCast ctl-sink.c, pinned revision](https://github.com/albfan/miraclecast/blob/0b7f1f1f6586dc65ff480f3cda5c2170a70aa020/src/ctl/ctl-sink.c),
especially `sink_handle_options`. This is an independently written small core;
no upstream implementation or dependency is vendored. Tests use synthetic
messages, not a sender capture. Reference behavior is not certification.

Our prototype explicitly validates response correlation, methods and deadlines;
the reference's initial response callback mainly logs. This stricter policy
requires interoperability testing, not a claim that every sender behaves so.

Response framing follows [RFC 2326](https://www.rfc-editor.org/rfc/rfc2326.html),
sections 4, 7 and 12. Tests now parse synthetic M1/M2 wire transcripts, every
M2 prefix, bounded binary bodies, errors, and a concatenated M2/M3 stream.
This is not a socket receive/send implementation: partial writes, live receive
queues and transport cancellation still need their own adapter.

Parameter syntax was cross-checked with
[libwds parameter grammar](https://chromium.googlesource.com/external/github.com/01org/wds/+/refs/tags/v0.1.0/libwds/rtsp/messages_rules.txt).
Field ordering and M4 composition were checked against
[AOSP WifiDisplaySource](https://android.googlesource.com/platform/frameworks/av/+/c4bd06130e4c3068ab58a0be88a4f765c2267563/media/libstagefright/wifi-display/source/WifiDisplaySource.cpp)
and its [VideoFormats implementation](https://android.googlesource.com/platform/frameworks/av/+/c4bd06130e4c3068ab58a0be88a4f765c2267563/media/libstagefright/wifi-display/VideoFormats.cpp).
Those implementations are references, not bundled code or certification. Their
advertised profiles are not copied wholesale into the laboratory profile.

See [the control/media roadmap](../../docs/miracast-control-development.md) and
[the unresolved platform gate](../../docs/miracast-platform-feasibility.md).
