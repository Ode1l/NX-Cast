NX-Cast IPTV
============

Local playlists:
- Copy .m3u or .m3u8 files into this directory.
- Extended M3U lists are expanded into the channel browser.
- HLS media/master playlists are shown as one directly playable stream.
- Relative local entries are resolved from the playlist directory.

Remote playlists:
- Open IPTV with X, switch to Sources with X, then press Y.
- Enter an HTTP/HTTPS M3U URL. NX-Cast downloads it to cache/playlists/.
- Select a source and press A to refresh it.
- Press ZR to configure a plain or gzip XMLTV URL.

Preinstalled remote playlists:
- Copy sources.example.txt to sources.txt and edit it on a computer.
- Put one HTTP/HTTPS M3U URL on each line, or use:
  Display name | M3U URL | optional guide.xml URL
- NX-Cast imports and deduplicates these entries at startup. New or changed
  entries are refreshed automatically.
- If sources.txt is copied while NX-Cast is running, press Y on Home to reload it.
- Removing a remote source in Sources also removes its entry from sources.txt
  and deletes its cached playlist and programme guide.

Packaging note:
- A sources.txt placed under the project assets/iptv directory is a distributor
  preset and is copied into locally built SD packages.
- A personal sources.txt should normally be stored directly on the Switch SD card.

Channels:
- ZL/ZR cycles All, Favorites, Recent, and playlist groups.
- Y toggles a favorite. L3 searches and R3 clears search.
- Up/Down or either stick vertically selects a channel.
- Left/Right, either stick horizontally, or L/R changes page.
- Touch a row to select it, then tap it again or PLAY CHANNEL to play.
- Swipe the list or tap the on-screen arrows to change page.
- tvg-logo images are cached asynchronously under cache/logos/.
- Current and next XMLTV programmes are matched using tvg-id.

Generated files such as sources.tsv, favorites.txt, recent.txt, and cache/
are maintained by NX-Cast. Do not edit sources.tsv; sources.txt is the
user-editable preinstall file. Press - on the home screen for a direct stream URL.

NX-Cast does not provide channels, credentials, DRM bypass, or regional
access. Only use playlists and streams you are authorized to access.
