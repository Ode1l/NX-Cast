# Local VS Code Diagnostic Workflow

> Recorded: 2026-08-02
>
> Scope: local editor integration only; this document is portable, the active
> `.vscode` files are not.

This record preserves the VS Code workflow used to rebuild a selected NX-Cast
network diagnostic profile, upload it with nxlink, and keep the nxlink server
attached. It intentionally records the stable task graph and profile contract
instead of committing machine-specific shell paths or Make invocations.

The physical AirPlay/DLNA observations and interpretation remain in
[`MACOS_HANDOFF_2026-07-23.md`](MACOS_HANDOFF_2026-07-23.md). The full profile
procedure remains in
[`AIRPLAY_FREEZE_DIAGNOSTICS.md`](AIRPLAY_FREEZE_DIAGNOSTICS.md).

## Stable Launch Contract

The Run and Debug entry is:

```json
{
  "name": "NX-Cast: Network Resource Diagnostic Matrix",
  "type": "node-terminal",
  "request": "launch",
  "preLaunchTask": "NX-Cast: AirPlay Diagnostic Profile Rebuild + Nxlink Upload + Server"
}
```

The `command` attached to this launch entry only prints a completion message.
All build, upload, and nxlink server work belongs to the pre-launch task. This
separation keeps the Run and Debug entry stable when the platform-specific task
command changes.

## Stable Task Flow

The pre-launch task performs this sequence:

1. enter the repository root using the current platform's native path rules;
2. expose a valid devkitPro environment to that shell;
3. read `${input:nxcastDiagProfile}` from the VS Code picker;
4. clean-build with that value as `NXCAST_DIAG_PROFILE` and require ImGui,
   libmpv, deko3d, and AirPlay Ed25519 support;
5. run `scripts/run_nxlink.sh` with `NXLINK_SERVER=1`, so the produced NRO is
   uploaded and log capture remains attached.

The platform-neutral part of the task contract is:

```bash
BUILD_MAKE_ARGS="NXCAST_DIAG_PROFILE=${input:nxcastDiagProfile} \
NXCAST_USE_IMGUI_UI=1 \
NXCAST_REQUIRE_LIBMPV=1 \
NXCAST_REQUIRE_DEKO3D=1 \
NXCAST_REQUIRE_AIRPLAY_ED25519=1" \
NXLINK_SERVER=1 ./scripts/run_nxlink.sh
```

This snippet is a contract, not a guaranteed copy-paste command. The local task
must still choose the correct shell, repository path, devkitPro initialization,
Make root arguments, and parallelism for its operating system.

## Diagnostic Profile Picker

The input ID is `nxcastDiagProfile`, and its default is
`full-owner-exclusive-observe-bsd12` (Profile 14).

Keep the picker values synchronized with `NXCAST_DIAG_PROFILES` in the local
checkout's Makefile:

| ID | Picker value |
|---:|---|
| 1 | `airplay-off` |
| 2 | `control-only` |
| 3 | `mdns-socket` |
| 4 | `mdns-idle` |
| 5 | `mdns-receive` |
| 6 | `full-parallel` |
| 7 | `full-serial` |
| 8 | `full-low-priority` |
| 9 | `mdns-receive-bsd8` |
| 10 | `mdns-receive-bsd16` |
| 11 | `full-discovery-suspend-bsd8` |
| 12 | `full-mdns-playback-suspend-bsd8` |
| 13 | `full-owner-exclusive-bsd12` |
| 14 | `full-owner-exclusive-observe-bsd12` |

The JSON option order does not determine the numeric ID; the Makefile does.
Always verify the startup log marker before treating a device run as valid.

## Platform Adapter Rules

Only the following values should change when reconstructing the local task:

| Concern | Windows devkitPro MSYS record | macOS or another environment |
|---|---|---|
| Shell | devkitPro's MSYS Bash | Use the shell where devkitPro and project packages are verified |
| Workspace path | Convert to an MSYS path; a short path was required for the space-containing workspace | Use the native repository path; do not add path conversion unless reproduced |
| Toolchain environment | Export `DEVKITPRO` and source the locally installed Switch environment script | Initialize `DEVKITPRO`, `DEVKITA64`, and `PATH` according to that installation |
| Make root workaround | The Windows task passed a stable `TOPDIR` and `THIS_MAKEFILE`; it never overrode GNU Make's automatic `CURDIR` | Start with the repository's native working directory and known-good Make command; add overrides only after reproducing a path failure |
| Parallelism | Fixed locally for the Windows build | Choose a value appropriate to the host |

The Windows workaround used an 8.3 short workspace path because the repository
directory contained spaces. Passing `CURDIR` on the Make command line was
explicitly avoided because it propagates through recursive Make and can prevent
the build-directory branch from being entered. This is historical evidence, not
a recommendation for macOS.

## Local-Only Files and Git Exclusions

Keep these machine adapters local unless a future cross-platform design is
agreed and tested:

- `.vscode/tasks.json`
- `.vscode/launch.json`
- any environment-specific `makefile` changes
- any environment-specific `.github/workflows/*` job changes

Before committing documentation, inspect the staged manifest explicitly. Do not
use blanket staging while these files are modified.

The completed local Windows path investigation is intentionally retained only
in the working copy at `plans/2026-07-22-vscode-space-path-build/`. Its reusable
findings are summarized above; its exact environment assumptions are not part
of the portable repository record.

## Reconstruction Checklist

1. Verify the intended shell can run the cross compiler and find required
   Switch packages.
2. Run the platform's known-good clean Profile 14 command directly from a
   terminal before putting it in VS Code.
3. Create the `nxcastDiagProfile` picker and diagnostic task using the stable
   names above.
4. Create the Run and Debug entry with the exact `preLaunchTask` reference.
5. Parse both JSON files and confirm VS Code resolves the pre-launch task.
6. Start one run, verify the Profile 14 startup marker, upload, nxlink heartbeat,
   and intentional exit.
7. Keep the local adapter unstaged when changing environments again.
