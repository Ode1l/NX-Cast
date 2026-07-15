# Switch Desktop Shortcut Plan

This document explains options for making `NX-Cast` easier to launch from the Switch home menu.

## Current State

The current release package is a homebrew app launched from `hbmenu`:

```text
sdmc:/switch/NX-Cast/NX-Cast.nro
sdmc:/switch/NX-Cast/dlna/
```

This is the safest distribution format for the current homebrew build.

## Goal

The user-facing goal is:

1. Launch `NX-Cast` from the Switch home menu.
2. Keep the SD package layout stable.
3. Avoid breaking the existing `hbmenu` route.
4. Avoid requiring source users to rebuild the application.

## Options

### Option 1: Keep hbmenu Only

Pros:

1. Lowest risk.
2. No additional title packaging.
3. Works with the current release zip.

Cons:

1. Less polished.
2. More steps for users.

Use this as the baseline.

### Option 2: Forwarder NSP

A forwarder NSP appears on the home menu and launches the NRO from SD.

Pros:

1. Better user experience.
2. Keeps the NRO update path simple.
3. Common pattern in homebrew distribution.

Cons:

1. Requires additional packaging tools.
2. Adds another artifact to CI/CD.
3. More signing/distribution caveats.
4. Higher support burden.

If adopted, the forwarder should point to:

```text
sdmc:/switch/NX-Cast/NX-Cast.nro
```

### Option 3: Full NSP Application

Pros:

1. Most console-native launch experience.

Cons:

1. Much more complex packaging.
2. Harder updates.
3. Larger legal and support surface.
4. Not aligned with the current homebrew release model.

This is not recommended for the current project stage.

## Packaging Direction

If a forwarder is added later, release artifacts should become:

```text
dist/NX-Cast.nro
dist/NX-Cast-sdmc.zip
dist/NX-Cast-forwarder.nsp
```

The SD zip should remain the primary install package.

## CI/CD Impact

Adding a forwarder requires:

1. A reproducible forwarder build tool.
2. CI installation of that tool.
3. Artifact upload in both continuous and tagged release workflows.
4. Documentation for users who choose not to install the forwarder.

Do not add a forwarder to CI until local packaging is reproducible.

## Official eShop Note

This homebrew launcher route is separate from official eShop distribution.

An official eShop app would require:

1. Nintendo developer registration and platform access.
2. Official SDK porting.
3. Nintendo review/certification.
4. A separate licensing audit.
5. Replacement of homebrew-only platform dependencies.

Do not confuse a homebrew forwarder with an official channel distribution strategy.

## Current Recommendation

Keep the current `hbmenu` release package as the baseline. Consider a forwarder only after playback, shutdown, UI, and release packaging are stable.
