# VS Code: macOS and Windows

The same Make targets and Bash commands are used on both platforms. Windows has
only a PowerShell adapter that starts devkitPro-compatible MSYS2 Bash. No WSL,
Docker or second copy of the build logic is required for local development.

## Run and Debug

| Launch | Behavior |
|---|---|
| `NX-Cast: Rebuild & Upload` | Clean, compile with Trace off, upload |
| `NX-Cast: Full Trace & Upload + Logs` | Clean, compile with media/input/AirPlay Trace on, upload and receive nxlink logs |
| `NX-Cast: Upload Only` | Upload the existing NRO without rebuilding |
| `NX-Cast: Publish GitHub Release` | Push committed main and its version tag; Actions builds/publishes the SD ZIP |

No incremental-build or AirPlay-off launch is exposed. Builds default to four
jobs without asking for input. Upload Only keeps the options already compiled
into the NRO. Logs are saved under `logs/run_nxlink-*.log`.

Tasks contain individual operations and two sequential composites needed by
`preLaunchTask` (which references one task, not an array). `Build Release` and
`Package SD ZIP` remain available as separate tasks for local packaging. Run them
in that order; local packaging does not publish anything.

## macOS

Install devkitPro and the project dependencies first. Set `DEVKITPRO` if the SDK
is not installed at `/opt/devkitpro`. The shared entry loads `switchvars.sh` and
adds the SDK tools to PATH. VS Code itself no longer hardcodes the SDK path.

```bash
bash scripts/dev.sh clean
bash scripts/dev.sh build
bash scripts/dev.sh upload
```

## Windows

Use devkitPro MSYS2, or MSYS2 configured with the devkitPro repositories and all
project dependencies. The adapter does not select `bash.exe` from PATH, because
that can be WSL or Git Bash rather than the required toolchain environment.

Default candidates are `DEVKITPRO\msys2\usr\bin\bash.exe`,
`C:\devkitPro\msys2\usr\bin\bash.exe` and `C:\msys64\usr\bin\bash.exe`.
For a custom installation, set these user environment variables, then restart
VS Code:

```powershell
[Environment]::SetEnvironmentVariable('NXCAST_MSYS_BASH', 'D:\devkitPro\msys2\usr\bin\bash.exe', 'User')
[Environment]::SetEnvironmentVariable('DEVKITPRO', 'D:/devkitPro', 'User')
```

Only use those example paths if they match your installation. The Bash adapter
normalizes Windows drive paths with `cygpath`. MSYS2 package installation uses
`pacman` without `sudo`; macOS continues using `dkp-pacman` and `sudo` when needed.
Both reuse the same pinned, SHA-256-verified FFmpeg package and skip installation
when it is already current. See [MSYS2 package management](https://www.msys2.org/docs/package-management/).

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev.ps1 clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev.ps1 build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev.ps1 upload
```

Use a workspace and SDK path without spaces for actual Make builds: this
repository's existing devkitPro Make rules do not reliably support spaces.
Shell argument quoting is not a substitute for Make path support.

Optional environment variables: `BUILD_JOBS` (default 4), `SWITCH_IP` (otherwise
nxlink discovery), `NXLINK_BIN` (custom executable), `NXLINK_RETRIES` (default 10).
Git must be available inside MSYS2 for publishing, with credentials configured.

## GitHub Release

1. Merge the intended changes to local `main`.
2. Set a new `APP_VERSION` in `makefile` and update `.github/release-notes.md`.
   Its first line must be `# NX-Cast v<VERSION> Release Notes`.
3. Commit all intended changes and ensure the working tree is clean.
4. Run `NX-Cast: Publish GitHub Release`.
5. Check the GitHub **Release** workflow. A successful push is not a successful
   build; the ZIP appears only after Actions completes.

Publishing does not build locally or require the Switch SDK. It refuses a dirty
tree, another branch, mismatched notes or an existing remote version tag. It
never auto-commits, merges, force-pushes, increments versions or overwrites a
release. Main and the tag are pushed atomically. A failed push can leave a local
tag; retry is allowed only if it still points to the current commit. If remote
main has advanced, integrate it first and inspect any local tag before retrying.

The already released `v0.3.2` cannot be published again through this command.
Toolchain releases remain managed separately; application publication reuses
the existing pinned FFmpeg/GHCR workflow.

## Validation Scope

`python3 scripts/test_dev_workflow.py` uses fake SDK/upload commands and local
bare Git repositories; it never contacts GitHub or a Switch. Windows adapter
execution still needs verification on a Windows machine. Historical diagnostic
documents describe older setups, not the current task names or launch menu.
