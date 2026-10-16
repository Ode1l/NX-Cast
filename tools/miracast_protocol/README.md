# Offline Miracast Control Prototype

This is **not a network receiver**. It implements only the M1/M2 OPTIONS
exchange. It does not open sockets, advertise a device, negotiate formats or
play media. It is excluded from the application build and release archive.

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

The advertised GET_PARAMETER/SET_PARAMETER methods are the next development
stage and are not implemented here. Do not expose this prototype on a network.

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

See [the control/media roadmap](../../docs/miracast-control-development.md) and
[the unresolved platform gate](../../docs/miracast-platform-feasibility.md).
