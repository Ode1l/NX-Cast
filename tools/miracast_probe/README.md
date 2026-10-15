# Miracast M0 Platform Probe

This standalone diagnostic gathers Horizon service-access evidence. It is not
a Miracast receiver and cannot appear in Windows/Android cast menus. It does
not initialize BSD sockets, alter Wi-Fi configuration, advertise capabilities,
inject frames, initialize LDN networking or call the player.

## Build And Run

From the repository root, with the same SDK environment used for NX-Cast:

```sh
bash scripts/build_miracast_probe.sh
```

Requires devkitA64, libnx and devkitPro tools only. No FFmpeg installation or
container is used. The shared environment script also supports MSYS on Windows;
the Windows execution path has not yet been tested for this probe.

Output: `build/miracast-probe/NX-Cast-Miracast-Probe.nro` and matching `.elf`.
Copy the NRO to the SD card and launch it through hbmenu, or send this exact NRO
with nxlink. The normal NX-Cast upload task sends the player, not this probe.
Press A once to collect evidence, then + to exit. The report is written to
`sdmc:/switch/NX-Cast/miracast-probe.log`; each run replaces the previous report.
Copy any previous report before rerunning. Failed SD writes are shown on screen.

## What It Measures

1. Firmware version and the libnx Atmosphere marker.
2. Service registration through Atmosphere's HasService command (TIPC 65100).
3. Whether the homebrew process can obtain each registered service handle.
4. For HOS 15+, whether `wlan` command 0 returns a wireless service object.
5. Closes all successfully acquired handles without opening radio communication.

Modern targets query `nifm:u`, `ldn:u`, `wlan`, `wlan:nd` and `wlan:p`.
Legacy targets query `nifm:u`, `ldn:u`, `wlan:inf`, `wlan:lcl` and `wlan:lga`.
Non-Atmosphere/unknown-version targets skip probing. No credentials, SSIDs,
MAC addresses or media URLs are read or logged.

`UNKNOWN` means the registry query failed. `ABSENT` means the query succeeded
and found no registration. `OPEN` only establishes access to a handle/object;
it does not establish Wi-Fi Direct, WFD advertisement or Miracast support.
An access failure can reflect permissions or service/session state and must
not be classified as missing hardware without further evidence.

The registry query avoids intentionally waiting on nonexistent services.
Acquisition still has a race if a service disappears, and Horizon synchronous
IPC has no application timeout in this probe. The display and SD report are
flushed before each access operation to identify a stalled call. This is one
reason the probe is separate from the media player. Use ordinary firmware and
hbmenu settings first; it does not require a new sysmodule or permission patch.

## Interface Provenance

The code is an NX-Cast diagnostic using libnx IPC primitives. No upstream
implementation is vendored. Interface facts were checked against:

- Atmosphere commit `6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8`,
  [SM extension source](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/sm/sm_ams.os.horizon.c):
  command 65100, eight-byte service name, low bit of the output byte.
- [Switchbrew WLAN services](https://switchbrew.org/wiki/WLAN_services#wlan):
  HOS 15+ service creator command 0 returns IWirelessCommunicationService.
- Installed libnx `sm.h`, `tipc.h`, `service.h` and `hosversion.h`.

See [the M0 inventory](../../docs/miracast-platform-feasibility.md) for the
remaining requirements. Device execution and real sender discovery are pending.
