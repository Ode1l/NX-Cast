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

## Startup Policy

DLNA startup uses four bounded optimizations without adding a proxy or format inference layer:

1. `SetAVTransportURI` still issues `loadfile` with `pause=yes`; the following `Play` keeps its normal DLNA meaning.
2. The first video frame is held for a 350 ms initial-seek window. If the controller immediately sends `Seek`, the loading screen stays visible until the seek's `playback-restart`, preventing a visible frame at zero followed by a jump. A timeout always releases the gate.
3. While state is `LOADING`, the frontend renders only the loading UI. It does not call the mpv video render API or wait on the video `done_fence`.
4. Direct `.mp4` URLs receive file-local fast-start cache options. HLS and unknown streams retain mpv defaults to avoid trading startup time for unstable playback.

Media trace builds add `t_ms` from the start of `SetAVTransportURI` to `Play`, `file-loaded`, `playback-restart`, startup-gate release, and the first presented video frame. Use `TRACE_MEDIA=1` when measuring startup.

## Related Documents

1. [player-layer.md](player-layer.md)
2. [dmr-implementation.md](dmr-implementation.md)
3. [scpd-module.md](scpd-module.md)
