#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/nxcast-ui-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cd "$ROOT"

"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isource \
    source/player/ui/browser.c scripts/test_iptv_browser.c -lm -o "$TMP/browser"
"$TMP/browser"
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isource \
    -Drename=home_test_rename source/player/ui/home.c scripts/test_home_ui.c -o "$TMP/home"
"$TMP/home"
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isource \
    source/player/ui/layout.c scripts/test_player_layout.c -o "$TMP/layout"
"$TMP/layout"
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Isource \
    -Iscripts/iptv_test_stubs source/player/ui/bar.c source/player/ui/timeline.c source/player/ui/home.c \
    source/player/ui/utf8.c scripts/test_player_title.c -o "$TMP/title"
"$TMP/title"
