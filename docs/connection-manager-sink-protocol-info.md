# ConnectionManager And SinkProtocolInfo

This document summarizes the current `ConnectionManager` and `SinkProtocolInfo` position.

## Current Decision

1. `ConnectionManager` is a real supported service, not just a placeholder.
2. `SinkProtocolInfo` has expanded from a narrow declaration to the current practical playback surface.
3. This area is continuous calibration, not a separate large design track.

## Ownership

`ConnectionManager` behavior is implemented in the DLNA control layer and described by the SCPD resources in `assets/dlna/`.

`SinkProtocolInfo` is maintained as a CSV template resource so it can be updated without embedding a large capability list in C code.

## Rules

1. Declare only capabilities that the current media stack can reasonably accept.
2. Keep declarations aligned with `libmpv/FFmpeg` support.
3. Expand gradually based on real controller compatibility tests.
4. Avoid pretending to support every DLNA profile.

## Related Documents

1. [dmr-implementation.md](dmr-implementation.md)
2. [scpd-module.md](scpd-module.md)
3. [source-compatibility.md](source-compatibility.md)
