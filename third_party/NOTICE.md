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
