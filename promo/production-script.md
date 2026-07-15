# NX-Cast Product Video Production Script

Target length: 28-30 seconds  
Master format: 1920x1080, 30 fps, 16:9  
Primary rule: show real NX-Cast and DLNA operation. Do not depict screen mirroring, AirPlay, DRM playback, or unsupported services.

## Shot Timeline

| Time | Source | Picture and operation | On-screen text | Chinese voice-over | English voice-over |
| --- | --- | --- | --- | --- | --- |
| 00:00-00:02 | Motion graphic or AI | NX-Cast logo appears from black. Cyan enters from the left and coral enters from the right. | `NX-CAST` | `让你的 Switch，成为一台 DLNA 媒体接收器。` | `Turn your Switch into a DLNA media receiver.` |
| 00:02-00:05 | Real footage | Show the Switch in landscape. Launch NX-Cast and hold on its ready state for one second. | `LAUNCH NX-CAST` / `启动 NX-Cast` | Continue previous line. | Continue previous line. |
| 00:05-00:09 | Real phone or desktop capture | Open a DLNA-capable media player, enter its renderer/device menu, and select `NX-Cast`. Keep the device name readable. | `SELECT NX-CAST` / `选择接收设备` | `打开 NX-Cast，在手机或电脑的 DLNA 播放器中选择它。` | `Launch NX-Cast, then choose it from a DLNA player on your phone or desktop.` |
| 00:09-00:12 | Real footage, preferably split screen | Tap the media item or Play on the sender. Cut to the Switch changing from ready state to playback. | `SELECT. CAST. PLAY.` | `发送媒体链接，视频立即在 Switch 上播放。` | `Send a media URL, and playback starts on the Switch.` |
| 00:12-00:16 | Real Switch capture | Let the video play cleanly for at least four seconds. Avoid extra UI and camera movement. | `LIBMPV PLAYBACK` | `播放由 libmpv 完成。` | `Playback runs through libmpv.` |
| 00:16-00:19 | Real sender capture plus Switch response | From the phone or desktop, pause and resume once. Show the Switch responding immediately. | `REMOTE CONTROL` / `发送端控制` | `你可以从发送端控制播放。` | `Control playback remotely from the sender.` |
| 00:19-00:23 | Real Switch capture | Show the player overlay. Perform one seek action, then briefly show volume adjustment. Do not stack several rapid button presses. | `CONTROLLER + TOUCH` / `手柄与触屏` | `也可以用手柄和触屏暂停、跳转和调节音量。` | `Or use the controller and touch overlay to pause, seek, and adjust volume.` |
| 00:23-00:26 | Motion graphic | Three terms appear in sequence: Discover, Control, Play. Use the existing cyan/coral visual system. | `SSDP  /  DLNA  /  LIBMPV` | `发现、控制、播放，一条完整链路。` | `Discovery, control, and playback in one complete pipeline.` |
| 00:26-00:30 | Motion graphic | End on the full logo and GitHub URL. Hold the final frame for at least two seconds. | `OPEN SOURCE. EXPERIMENTAL.`<br/>`github.com/Ode1l/NX-Cast` | `NX-Cast，开源、实验性，正在持续完善。` | `NX-Cast is open source, experimental, and evolving.` |

## Chinese Voice-over

让你的 Switch，成为一台 DLNA 媒体接收器。打开 NX-Cast，在手机或电脑的 DLNA 播放器中选择它。发送媒体链接，视频立即在 Switch 上播放，播放由 libmpv 完成。你可以从发送端控制播放，也可以用手柄和触屏暂停、跳转和调节音量。发现、控制、播放，一条完整链路。NX-Cast，开源、实验性，正在持续完善。

## English Voice-over

Turn your Switch into a DLNA media receiver. Launch NX-Cast, then choose it from a DLNA player on your phone or desktop. Send a media URL, and playback starts on the Switch through libmpv. Control playback remotely from the sender, or use the controller and touch overlay to pause, seek, and adjust volume. Discovery, control, and playback in one complete pipeline. NX-Cast is open source, experimental, and evolving.

## Real Footage Recording List

Record each action as a separate clip. Leave one second of still footage before and after every action.

1. Switch launches NX-Cast and reaches its ready state.
2. Phone or desktop opens the DLNA renderer list.
3. `NX-Cast` is selected from the renderer list.
4. A media item or URL is sent.
5. Switch changes from ready state to video playback.
6. Four to six seconds of uninterrupted playback.
7. Sender pauses and resumes playback once.
8. Switch player overlay appears.
9. One seek operation.
10. One volume adjustment.

Recording requirements:

- Record landscape footage at 1080p; use 60 fps if available.
- A dock capture card is preferred for the Switch screen.
- Record the sender screen directly instead of filming the phone display when possible.
- Disable notifications and hide personal device names, IP addresses, and account information.
- Use owned, public-domain, or properly licensed media.
- Avoid rapid taps. Hold every important state long enough to read.
- Keep the original recordings clean; add titles and zooms only during editing.

## AI B-roll Options

AI should be used only for atmosphere and transitions. Do not generate fake NX-Cast menus or fake product behavior.

### Opening Shot Prompt

```text
Use case: open-source software product teaser
Primary request: a clean tabletop scene with a generic handheld game console and a smartphone prepared for wireless media casting
Action: a soft cyan light travels from the phone toward the handheld console, followed by a coral rim light
Camera: slow controlled push-in, eye-level product shot
Lighting/mood: minimal studio lighting, precise and technical, warm off-white environment
Color palette: black, warm white, cyan, coral
Style/format: premium motion-design product b-roll, 16:9
Constraints: no people, no hands, no logos, no readable UI, no text, no copyrighted characters
Avoid: fake app interfaces, extra devices, flicker, warped screens
```

### Network Transition Prompt

```text
Use case: transition between device selection and real playback footage
Primary request: an abstract media packet travels through a clean local network from a phone-shaped outline to a handheld-console-shaped outline
Action: one cyan pulse crosses the frame and becomes coral as it reaches the receiver
Camera: locked orthographic composition
Lighting/mood: crisp, modern, technical
Color palette: black, warm white, cyan, coral
Style/format: flat vector motion graphic, 16:9
Constraints: no text, no logos, no people, no fake UI
Avoid: 3D clutter, neon cyberpunk styling, visual noise, jitter
```

## Edit Notes

- Keep the real selection and playback sequence continuous enough to prove the product works.
- Use hard cuts for user actions and short 6-10 frame dissolves for branding transitions.
- Do not speed up the moment where `NX-Cast` is selected; the device name must remain readable.
- Use subtle punch-in crops on sender footage, but keep the Switch playback footage stable.
- Place Chinese or English titles in the safe center area so the same master can be cropped later if needed.
- Export separate Chinese and English masters rather than placing both subtitle languages on every frame.
