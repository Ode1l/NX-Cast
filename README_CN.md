# NX-Cast

`NX-Cast` 是运行在 Atmosphère Homebrew 环境下的 Nintendo Switch 开源媒体接收器。

当前主线目标是把 Switch 上的通用 `DLNA DMR` 接收器做扎实，并采用直接的 `protocol -> renderer -> libmpv` 播放路径，而不是继续维护一套独立的入口建模和策略流水线。

## 当前状态

项目已经完成并验证过这些基础能力：

- `SSDP` 发现
- 运行时输出的 `Description.xml` 与各服务 `SCPD`
- `SOAP` 控制链：`SetAVTransportURI / Play / Pause / Stop / Seek`
- `GENA` 事件订阅与 `LastChange`
- renderer 的 snapshot/event 状态桥
- `libmpv` 后端
- `ao=hos`
- `deko3d/libmpv render API`
- 基于手柄的播放器 OSD 与本地 seek/音量控制

当前更准确的阶段是：

1. 通用 `DMR` 底座已成立
2. 协议状态与运行时播放状态已经围绕同一播放会话收口
3. 直接 `URL -> libmpv` 的播放链已经落地
4. 安装自定义媒体工具链后，`deko3d` 已成为首选渲染路径
5. `hwdec=nvtegra` 已接入运行时选项，但是否真正生效仍取决于所安装的 `FFmpeg/libmpv`

## 当前设计原则

项目当前按这几条原则推进：

1. 结构重构与行为变化分开做
2. 协议层只做协议职责，不变成站点兼容 hack 层
3. 协议动作直接调用 renderer
4. `libmpv` 负责 URL 探测、拉流、demux、decode 与播放
5. renderer 订阅 `libmpv` 属性和事件，再把运行时状态同步回协议层
6. `SOAP`、`LastChange`、兼容查询都读取同一份协议观察状态

一句话说：

**协议层向下发命令，renderer 把真实运行时状态再向上同步。**

## 当前架构

```text
main
  -> protocol/dlna
       -> discovery (SSDP)
       -> description (template XML / CSV)
       -> control (SOAP / GENA / protocol_state)
            -> renderer facade
  -> player
       -> core (session / snapshot / event pump)
       -> backend (libmpv / mock)
       -> render (view / frontend)
```

当前两条关键状态线：

1. renderer 维护真实播放会话
2. `protocol_state` 维护协议观察状态

## 当前播放模型

当前播放路径刻意保持很薄：

```text
SetAVTransportURI
  -> renderer_set_uri(...)
  -> libmpv loadfile
  -> libmpv 自动探测 URL / demux / decode
  -> 属性与事件回调
  -> protocol_state 同步
```

当前回收并同步的运行时字段包括：

- `time-pos`
- `duration`
- `pause`
- `mute`
- `seekable`
- `paused-for-cache`
- `seeking`
- EOF / error 状态

项目当前已经不再保留独立的预处理播放层。

## 当前后端路线

当前正式后端路线是：

1. `ao=hos`
2. `deko3d/libmpv render API`
3. `libmpv` 继续作为播放器核心
4. 当自定义媒体工具链不存在时，`OpenGL/libmpv render API` 作为回退路径

需要特别区分：

- `OpenGL` / `deko3d` 是渲染路径
- `hwdec=nvtegra` 是解码路径

当前结论：

1. `hos-audio + deko3d` 已接通
2. 运行时 `hwdec=nvtegra` 偏好已经接入
3. 官方 `dkp` 工具链仍主要对应 OpenGL 回退路径
4. 当前推荐基线是 `wiliwili` 这套自定义 `FFmpeg/libmpv` 包

## 当前工作重点

当前优先级是：

1. 继续补通用 `DMR` 兼容面
2. 提高协议状态与控制端进度同步的准确性
3. 保持模板化描述层与真实实现一致
4. 在真实 URL 和真实控制端上继续加固播放稳定性
5. 继续加固新的 `deko3d` 渲染路径和手柄播放器 UI
6. 把 `nvtegra` 验证建立在自定义媒体工具链上，而不是官方回退包上

## 目录

```text
source/
  main.c
  log/
  player/
    core/
    backend/
    render/
  protocol/
    dlna/
      discovery/
      description/
      control/
    http/
assets/
  dlna/

Switch 运行时资源目录：
  sdmc:/switch/NX-Cast/dlna/
```

## 推荐阅读顺序

1. [docs/Player层设计.md](docs/Player层设计.md)
2. [docs/DMR实现细节.md](docs/DMR实现细节.md)
3. [docs/SCPD模块说明.md](docs/SCPD模块说明.md)
4. [ROADMAP.md](ROADMAP.md)

## 构建

需要：

- `devkitPro`
- `devkitA64`
- `libnx`
- 推荐安装 `wiliwili` 提供的自定义媒体包：
  - `libuam`
  - `switch-ffmpeg`
  - `switch-libmpv_deko3d`

推荐本地安装：

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  $base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst \
  $base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst \
  $base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst
```

构建：

```bash
make
```

输出：

- `NX-Cast.nro`

Docker 构建：

```bash
docker build -t nx-cast-build .
docker run --rm -e DEVKITPRO=/opt/devkitpro -v "$PWD:/workspace" -w /workspace nx-cast-build bash -lc 'make clean && make -j$(nproc)'
```

## 当前文档说明

仓库文档按当前实现维护，不再保留已经删除的旧设计表述。  
如果文档和代码冲突，以 `source/` 当前实现为准，并应继续更新文档而不是保留过时描述。
