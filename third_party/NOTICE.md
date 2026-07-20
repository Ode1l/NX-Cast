Third-party components
======================

Dear ImGui
----------

Source: https://github.com/ocornut/imgui
Version: v1.91.9b
License: MIT, see `third_party/imgui/LICENSE.txt`.

Switch font asset
-----------------

`assets/fonts/switch_font.ttf` was imported from the local wiliwili reference
project. The font license text is kept in `assets/fonts/LICENSE.SourceFont.txt`.

NXMP ImGui deko3d shader reference
----------------------------------

The embedded ImGui deko3d shader binaries in
`source/player/render/imgui/imgui_shaders.inc` were generated from the local
NXMP reference project's `romfs/shaders/imgui_vsh.dksh` and
`romfs/shaders/imgui_fsh.dksh`.

libsodium
---------

Source: https://github.com/jedisct1/libsodium
Version: devkitPro `switch-libsodium` 1.0.20-1 package selected by the build image.
License: ISC, see `assets/licenses/LICENSE.libsodium.txt`.

NX-Cast links libsodium for the Ed25519 operations required by AirPlay device
identity and pairing. No libsodium source is vendored in this repository.

PlayFair compatibility backend
------------------------------

Source: https://github.com/FDH2/UxPlay
Commit: `3ca7526387e894d6848b84c209de361c3bedd1ec`
License: GPL-3.0 for PlayFair; LGPL-2.1-or-later for the isolated response-table
wrapper. See `third_party/playfair/LICENSE.md` and
`third_party/playfair/PROVENANCE.md`.

NX-Cast vendors only the PlayFair compatibility subset, not UxPlay's server,
pairing, media, rendering, or platform code. The upstream project describes
the legal status of this compatibility technique as unclear. Inclusion is for
open-source research/homebrew interoperability and does not imply Apple
authorization, MFi certification, DRM support, or legal suitability in every
jurisdiction.
