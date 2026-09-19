# AirPlay Protocol Compatibility Contract

NX-Cast implements its AirPlay receiver protocol in C for the Switch runtime. UxPlay and RPiPlay are behavior references, not linked or ported runtime dependencies.

## Connection Identity

An AirPlay TCP connection is not automatically a media session. Pairing and FairPlay state remain private to that connection, while AirPlay URL playback can use several HTTP connections carrying one stable `X-Apple-Session-ID`.

| Connection class | Recognition | Identity used for media | Lifetime |
|---|---|---|---|
| RAOP | Valid `CSeq` on the RTSP connection | TCP/RTSP connection and negotiated stream identity | Ends with that connection or typed stream teardown |
| AirPlay HTTP | HTTP/1.1, secondary pairing, or `X-Apple-Session-ID` | Exact bounded Apple session id mapped to an internal token | Ends after the final bound connection closes |
| BLE information | RTSP `/info` query containing `txtAirPlay` or `txtRAOP` without `CSeq` | TCP connection id | Information request only |

The Apple session value is never logged. A connection cannot change its bound value or switch between incompatible connection classes. Apple Session ID is authoritative for HTTP `/play` and reverse identity. Peer address is routing metadata and never joins RAOP to URL playback because multiple senders can share an address. Remote-video routes cannot reach player ownership before the connection is bound to a logical session.

## Pipeline Boundaries

AirPlay failures are classified at the first boundary that does not complete:

1. `control`: socket accepted and request parsed.
2. `pairing`: pair setup/verify and FairPlay authorization completed.
3. `transport`: timing/audio/mirror ports allocated.
4. `stream`: RECORD accepted and media receiver listening.
5. `media`: first codec configuration, keyframe, audio, or URL received.
6. `bridge`: media bytes accepted by the Switch mux/bridge.
7. `player`: central ownership granted, remote session promoted active, and libmpv load issued.
8. `decoder`: libmpv/FFmpeg reports decoded media.

Protocol success is not inferred from an iPhone UI alone. A real playback attempt is only known to have crossed the protocol layer when the trace reaches `media`; decoder investigation begins only after `bridge` and `player` are present.

## Ownership Rules

- DLNA, IPTV, AirPlay URL video, and AirPlay mirroring continue to use the existing `protocol_coordinator` and player media actor.
- AirPlay discovery and pairing cannot directly call libmpv or release another protocol's lease.
- A stale runtime generation cannot stop or release a newer AirPlay playback generation.
- Closing one TCP connection cannot stop URL playback while another connection remains bound to the same logical Apple session.
- The final logical connection close clears only pending Reverse HLS negotiation. Once URL playback is active, ordinary HTTP or reverse TCP close does not infer a media stop.
- Active URL playback ends on explicit `/stop`, a valid replacement `/play`, player EOF/error, cross-protocol takeover, or AirPlay/application shutdown. There is no disconnect grace timer.
- `TEARDOWN` is scoped to the negotiated RTSP stream types. RAOP audio or mirror teardown cannot stop unrelated URL playback.
- Cross-protocol takeover submits a player stop and synchronously releases the exact previous AirPlay owner before the new coordinator claim succeeds. A failed or stale release rejects the new claim and leaves the current owner intact.
- Reverse HLS `/play` establishes session-local negotiation without claiming `airplay-video`; ownership and player load wait for the final `/action` `ACTION_READY`.
- `ACTION_READY` claims `airplay-video`, promotes the remote session to active, and then issues `load()` and `play()` under the session state lock. A load failure stops and releases the just-claimed owner.
- Reverse HLS `ACTION_READY` fails closed with `503` when no owner-claim callback is configured; no player mutation is attempted.
- Direct URL `/play` performs its optional claim outside the state lock, then revalidates the session generation. If stop or session close wins the race, the just-claimed owner is released and the request returns `409` without loading or playing.
- The renderer presents the loading layer until libmpv reports the current file loaded and releases its startup gate. An old deko3d surface cannot be reported as the replacement generation's first frame.
- Audio-only `SETUP`/`RECORD` is accepted as a valid protocol mode, but standalone playback is currently an explicit diagnostic stub. It does not claim player ownership or load `airplay://mirror`; the first received packet is logged and payloads are discarded.
- Type-110 setup for the same logical session leaves the diagnostic-only audio state and proceeds through the normal combined mirror ownership and LOAD path.
- A failed audio-only record callback rejects `SETUP`/`RECORD` with `461`, resets the negotiated transport state, and reports an integration error.

## Route Contract

The table records the stable video-receiver subset. A successful protocol response only means that the request was accepted; player mutation still goes through the coordinator and player actor.

| Route or method | Accepted form | Success | Stable rejection | Effect |
|---|---|---:|---:|---|
| `OPTIONS *` | RTSP/HTTP `OPTIONS` | 200 | - | Advertises the supported control methods. |
| `/info`, `/server-info` | `GET` | 200 | 501/500 | Returns receiver capability information. |
| `/fp-setup` | `POST` binary body | 200 | 400/405 | Advances the connection-local FairPlay exchange. |
| `/reverse` | HTTP/1.1 `POST` with a logical Apple session id | 101 | 400/405 | Registers or replaces the session's serialized PTTH event channel. |
| `/play` | `POST` with an absolute URL or FCUP HLS locator | 200 | 400/409/503 | Direct URLs may claim AirPlay URL ownership and revalidate the claim generation; HLS waits for bounded reverse responses before actor load. |
| `/action` | `POST` binary plist | 200 | 400/405/409/503 | Correlates reverse HLS responses by session, request id, and URL; `FCUP_Response_StatusCode` is advisory. YouTube condensed media playlists may use empty `PARAMS`; `ACTION_READY` claims and activates before load/play and a missing claim callback fails closed. |
| `/rate`, `/scrub`, `/playback-info`, `/stop` | AirPlay remote-video methods | 200 | 400/405/409 | Enqueues playback control through the existing owner. |
| `SETUP` | Timing, audio type 96, or mirror type 110 plist | 200 | 400/455/461 | Allocates transport state; audio may arrive before video. |
| `RECORD` | After initial transport setup | 200 | 455/461 | Enables media ingress; audio-only sessions remain diagnostic-only, while a runtime failure rejects and resets the setup. |
| `GET_PARAMETER`, `SET_PARAMETER` | Setup or recording state | 200 | 400/455 | Handles the bounded parameter subset. |
| `FLUSH` | Recording state | 200 | 455 | Acknowledges sender discontinuity; it does not directly mutate libmpv or FFmpeg buffers. |
| `TEARDOWN` | Open RTSP control session | 200 | 455 | Idempotently closes only the negotiated audio/mirror stream types in that RTSP session. |
| Unknown route | Any unsupported request | - | 501 | No fallback player behavior. |

`/airplay-hls/<token>/...` is loopback-only, generation-bound, and returns 404 after reset or teardown. Reverse replacement closes the stale socket so only one writer remains registered for a logical session.

## Explicit Non-goals

- AirPlay 2 multi-room audio and music-only receiver behavior.
- UxPlay's GStreamer pipeline, local HLS server, Linux socket runtime, or UI.
- A generic cross-protocol state-machine framework. Each protocol owns its control state; only player ownership is global.
