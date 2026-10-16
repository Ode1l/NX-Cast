# Miracast Platform Feasibility

Updated: 2026-10-16. M0 is **not passed**. A standalone service probe builds,
but device access, discovery and sender connection have not been tested.

Offline protocol development can continue independently: see the
[control/media roadmap](miracast-control-development.md). The initial OPTIONS
exchange is host-tested but is not integrated into the app or a live socket.

## Branches And Baselines

- `main`: unchanged at `0efdba4` when this work started.
- `codex/rtsp-refactor`: checkpoint `2def19a`, common request-parser extraction;
  existing host tests and Switch build passed, device regression pending.
- `codex/miracast`: starts from that checkpoint. Research tools remain outside
  the production source tree and normal release packaging.

The user authorized isolated Miracast development while device tests are
unavailable. This does not turn the refactoring checkpoint into a device-tested
baseline. Run the [refactoring device checklist](../plans/2026-10-15-rtsp-extraction/steps/step-1.md)
before merging either branch into main.

## Capability Inventory

| Required capability | Evidence and candidate | Status |
|---|---|---|
| Access to wireless service | libnx wraps legacy `wlan:inf`; firmware 15+ has a different `wlan` creator according to Switchbrew. | Probe implemented; runtime pending. |
| WFD discovery information elements | The modern interface lists AddVendorSpecificIe and frame operations. | Candidate only; argument layouts, permitted states and discoverability unverified. |
| P2P discovery and group formation | No turnkey Wi-Fi Direct API was found in inspected libnx headers. LDN is Nintendo local networking, not proof of WFD compatibility. | No validated implementation path. |
| Management/action frames | Modern interface lists PutActionFrame/GetActionFrame; older local service has related commands. | Candidate only; permissions, filtering and radio-mode interaction unverified. |
| Addressing after group formation | Existing BSD sockets serve ordinary LAN traffic. | P2P interface/address assignment and DHCP ownership unknown. |
| Infrastructure route | Microsoft's MS-MICE supports an existing network and documents Wi-Fi Direct discovery. | Potential separate route; does not remove discovery work. |
| RTSP negotiation | NX-Cast now has a common request parser. | WFD negotiation, response parsing and transaction/state logic remain to implement. |
| Media playback | NX-Cast has FFmpeg/libmpv/nvtegra/deko3d and player ownership. | Miracast RTP/MPEG-TS transport and negotiated formats are untested. |
| Teardown/coexistence | Current player and protocol lifecycle facilities can be reused. | Wireless teardown and infrastructure/P2P coexistence still need device evidence. |

The older header-only search was insufficient to evaluate modern firmware.
Switchbrew describes a replacement wireless interface with useful low-level
operations. This justifies an access experiment, not a conclusion that the
existing stock Horizon interfaces can already run Miracast. Chipset capability,
service registration, service permission and working WFD behavior are four
different questions.

## First Executable Experiment

See [tools/miracast_probe](../tools/miracast_probe/README.md). Build with:

```sh
bash scripts/build_miracast_probe.sh
```

The independent NRO checks service registration through Atmosphere SM, attempts
to obtain registered service handles, and on modern firmware calls only the
documented service-creation command. It closes the handles afterwards. It does
not call OpenCommunication, add IEs, change channels, send frames or initialize
a P2P/LDN network. No new service is advertised by NX-Cast.

The saved report distinguishes failed queries from confirmed absence and
includes raw result codes. A successfully opened wireless object still leaves
the main Miracast capabilities untested. An unavailable or permission-denied
interface identifies the next research task; it is not proof that the chip
cannot support the protocol.

## Next Gates

1. Run the probe on the intended Atmosphere/HOS version through normal hbmenu;
   retain the report and the exact ELF if the experiment crashes.
2. If `wlan` access and child creation succeed, establish version-specific IPC
   argument layouts and required service state from evidence. Do not guess
   layouts from command names. Check whether radio communication can be opened
   and closed without disrupting the existing infrastructure connection.
3. Demonstrate WFD device discovery and P2P group establishment using an
   unmodified target sender. Record addressing and teardown behavior.
4. Only after a usable path is demonstrated, integrate a bounded WFD control
   session, then RTP/MPEG-TS input, then synchronized audio through the player.

If step 2 requires an external service, firmware patch or another operating
system, document the requirement as a separate product decision. A custom
sender connecting by IP is a laboratory test, not completion of the native
Windows/Android discovery gate. Service probe success alone does not pass M0.

## Validation So Far

- Standalone NRO/ELF built with devkitA64 and libnx, `-Wall -Wextra -Werror`.
- Build uses no FFmpeg/mpv downloads and no sockets or production worker threads.
- Production source, root Makefile, release workflows and package script are
  unchanged relative to `2def19a`.
- Existing player NRO/ELF checksums still match the refactoring handoff.
- Real-device and Windows-host build validation remain pending.

## References

- [Switchbrew WLAN services](https://switchbrew.org/wiki/WLAN_services): service generations, object creation and command inventory. This is reverse-engineered documentation; validate layouts for the target firmware.
- [libnx wlaninf header](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/wlaninf.h): legacy service wrapper and firmware range.
- [Atmosphere HasService interface implementation](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libstratosphere/source/sm/sm_ams.os.horizon.c): pinned registration-query contract; no upstream implementation is bundled.
- [Microsoft infrastructure projection](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/wireless-projection-implementing-over-existing-network): discovery and existing-network route.
- [MiracleCast](https://github.com/albfan/miraclecast): Linux sink reference, dependent on wpa_supplicant and Linux services rather than a portable Switch networking backend.
