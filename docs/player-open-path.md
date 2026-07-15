# Player Open Path

This document keeps the current media opening decision explicit.

## Current Decision

1. The player no longer has a separate resource-selection layer.
2. After `SetAVTransportURI`, the protocol layer calls the renderer.
3. The renderer sends `loadfile` to `libmpv`.
4. Format probing, network access, demuxing, and decoding are owned by `libmpv/FFmpeg`.

## Current Path

```text
SetAVTransportURI
  -> renderer_set_uri
  -> player set_media
  -> backend/libmpv loadfile
  -> libmpv observed properties / events
  -> PlayerSnapshot / PlayerEvent
```

## Rules

1. Do not add another inference pipeline inside `player`.
2. Do not manually hint media formats unless a precise media-stack bug requires it.
3. Do not let the protocol layer know backend internals.
4. Focus compatibility work on state synchronization and protocol interoperability first.

## Related Documents

1. [player-layer.md](player-layer.md)
2. [dmr-implementation.md](dmr-implementation.md)
3. [scpd-module.md](scpd-module.md)
