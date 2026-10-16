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
- M2 response decoding is **not implemented**. The test directly supplies its
  normalized CSeq/status/Public fields. The future adapter must validate the
  RTSP response framing, duplicates and body length before invoking this API.
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

See [the control/media roadmap](../../docs/miracast-control-development.md) and
[the unresolved platform gate](../../docs/miracast-platform-feasibility.md).
