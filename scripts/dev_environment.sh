#!/usr/bin/env bash
# Sourced by developer commands; keep SDK setup out of VS Code JSON.
nxcast_load_sdk() {
    export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
    case "$(uname -s)" in
        MSYS*|MINGW*) DEVKITPRO=$(cygpath -u "$DEVKITPRO") ;;
    esac
    if [[ ! -f "$DEVKITPRO/switchvars.sh" ]]; then
        echo "Switch SDK not found at $DEVKITPRO. Set DEVKITPRO to your installation." >&2
        return 1
    fi
    source "$DEVKITPRO/switchvars.sh"
    export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/pacman/bin:$PATH"
}
