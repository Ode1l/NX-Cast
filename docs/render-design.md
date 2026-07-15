# Render Design

This document describes the current render and video presentation strategy.

## Current Goal

The renderer should provide a stable playback surface for `libmpv` while keeping protocol and player state independent from graphics implementation details.

The preferred release path is:

```text
libmpv
  -> deko3d render API
  -> Switch framebuffer
  -> player overlay
```

OpenGL remains a fallback path for development and minimal compatibility.

## Render Paths

### deko3d

The `deko3d/libmpv render API` path is the preferred path when the installed media toolchain provides:

1. `mpv/render_dk3d.h`
2. `libmpv.a` linked with `deko3d`
3. `libuam`
4. `FFmpeg` with Switch `nvtegra` hardware decode support

This path is used for release builds when `NXCAST_REQUIRE_DEKO3D=1` succeeds.

### OpenGL

The OpenGL path is a fallback when `deko3d` is unavailable.

It is useful for:

1. Keeping the project buildable with a wider devkitPro setup.
2. Debugging protocol and player logic without the full media stack.
3. Avoiding accidental hard dependency on one render backend during development.

It is not the default release target.

### Software Fallback

Software rendering is a last-resort path. It is not expected to provide production playback quality.

## Responsibilities

### player/backend/libmpv

Owns:

1. `mpv_handle`
2. `mpv_render_context`
3. Render API attachment
4. Render frame calls
5. Backend event pumping

### player/render

Owns:

1. Foreground video page lifecycle.
2. Graphics device and swapchain lifecycle.
3. Frame acquisition and presentation.
4. Overlay composition order.

### player/ui

Owns:

1. Overlay state.
2. Timeline drawing.
3. Control drawing.
4. Input-to-command mapping.

## Frame Order

The intended frame order is:

```text
acquire frame
  -> render mpv video
  -> render player overlay
  -> present frame
```

If future GUI work adds ImGui or another UI layer, that layer should be composed after player video and before present. Only one system should acquire and present per frame.

## Backend Attachment

The backend should create and initialize `mpv` first, then attach the render context after the graphics context is ready.

This avoids loading media before render context creation and matches the practical behavior expected by `libmpv`.

## Hardware Decode

`deko3d` is not hardware decode by itself. Hardware decode depends on the installed FFmpeg/mpv package exposing `nvtegra` support.

Current runtime preference:

1. Use explicit `hwdec=nvtegra` when detected.
2. Fall back to `hwdec=yes`.
3. Keep playback functional even if hardware decode is unavailable.

## Shutdown

Render shutdown should release resources in this order:

1. Stop player commands and event pumping.
2. Detach/free `mpv_render_context`.
3. Terminate/destroy `mpv_handle`.
4. Release overlay and frontend resources.
5. Release graphics/platform resources.

This avoids freeing graphics or platform state while `libmpv` may still reference render resources.

## Current Focus

1. Keep `deko3d` stable as the release path.
2. Keep OpenGL as a clear fallback, not a competing product target.
3. Avoid render-thread blocking from input handling.
4. Keep UI drawing independent from protocol and backend internals.

## Related Documents

1. [player-layer.md](player-layer.md)
2. [libmpv-dependencies.md](libmpv-dependencies.md)
3. [ffmpeg-mpv-toolchain.md](ffmpeg-mpv-toolchain.md)
