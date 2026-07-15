# NX-Cast

`NX-Cast` 是运行在 Atmosphere Homebrew 环境下的 Nintendo Switch 媒体接收器。

当前产品目标是做一个扎实的通用 `DLNA DMR`：手机、桌面播放器、电视应用通过 DLNA 把媒体 URL 发给 Switch，Switch 再通过 `libmpv` 播放。

## 当前状态

当前主线已经具备：

- `SSDP` 发现
- 运行时输出 `Description.xml` 和服务 `SCPD`
- `SOAP` 控制：`SetAVTransportURI`、`Play`、`Pause`、`Stop`、`Seek`、音量
- `GENA` 事件订阅和 `LastChange`
- 协议状态从真实播放会话同步
- `libmpv` 后端和 `ao=hos`
- 首选 `deko3d/libmpv render API` 视频路径
- 在媒体工具链支持时请求 `hwdec=nvtegra`
- 静态主页，展示投屏教程、运行状态和最后一条错误
- 手柄和触摸播放器 overlay
- Docker 和 GitHub Actions 发布构建

项目仍然是实验性 Switch homebrew。当前开发重点是 DLNA 兼容性、播放稳定性和播放器 UI。

## 当前不做什么

`NX-Cast` 当前不是：

- DLNA 媒体服务器，也就是 `DMS`
- DLNA 控制器，也就是 `DMC`
- AirPlay 接收器
- iQiyi、MangoTV、CCTV、Bilibili 或 IPTV 的站点原生客户端
- DRM 绕过或站点登录实现

当前播放链保持很薄：DLNA 只提供 URL，后续探测、网络请求、demux、decode 和播放交给 `libmpv/FFmpeg`。

## 安装

优先使用 GitHub Release 里的发布包：

1. 下载 `NX-Cast-sdmc.zip`
2. 解压到 Switch SD 卡根目录
3. 从 `hbmenu` 启动 `switch/NX-Cast/NX-Cast.nro`

发布包结构：

```text
switch/NX-Cast/NX-Cast.nro
switch/NX-Cast/dlna/
```

`switch/NX-Cast/dlna/` 里是运行时需要的 DLNA XML、CSV、HTML 和图标资源。

## 控制

空闲时，软件显示主页：基础投屏教程、运行状态和最后一条错误。完整日志历史仍保留给调试使用，但不会作为发布版前台 UI 展示。

视频播放时：

- `A`：播放 / 暂停
- `+`：退出程序
- `-`：显示控制栏
- `L` 或 `Left`：后退 10 秒
- `R` 或 `Right`：前进 10 秒
- `Up` / `Down`：调高 / 调低音量
- 左摇杆或右摇杆横向：seek
- 左摇杆或右摇杆纵向：音量
- 触摸屏点击：显示控制栏
- 控制栏显示时点击中间按钮：播放 / 暂停
- 触摸拖拽进度条：预览目标时间，松手后跳转

## 架构

```text
main
  -> protocol/dlna
       -> discovery
       -> description
       -> control
       -> protocol_state
  -> protocol/http
  -> player
       -> core
       -> backend
       -> render
       -> ui
```

关键状态流：

```text
SetAVTransportURI
  -> renderer_set_uri
  -> libmpv loadfile
  -> libmpv properties/events
  -> PlayerSnapshot / PlayerEvent
  -> protocol_state
  -> SOAP query / GENA notify
```

协议命令向下发给播放器。真实运行时状态再从播放器向上同步，成为协议观察状态。

## 构建

### 推荐：Docker

这是最省事的路径。它和 GitHub Actions 使用同一套媒体包，并且直接生成可发布的 SD 卡包。

```bash
./scripts/docker_build_release.sh
```

输出：

```text
dist/NX-Cast.nro
dist/NX-Cast-sdmc.zip
```

Docker 构建会安装当前推荐的 `wiliwili` 媒体包：

- `libuam`
- `switch-ffmpeg`
- `switch-libmpv_deko3d`

### 本地 devkitPro 构建

需要：

- `devkitPro`
- `devkitA64`
- `libnx`
- `switch-libmpv_deko3d`
- `switch-ffmpeg`
- `libuam`

安装当前推荐的预编译媒体包：

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  "$base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst" \
  "$base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst" \
  "$base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst"
```

构建：

```bash
source /opt/devkitpro/switchvars.sh
make NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j2
NXCAST_MIN_NRO_SIZE=5000000 ./scripts/package_release.sh
```

严格参数用于防止误生成没有链接 `libmpv/deko3d` 的小体积 mock/fallback `NRO`。

用于排查播放和输入问题的 trace 构建：

```bash
source /opt/devkitpro/switchvars.sh
make trace
```

`make trace` 会先 clean，再打开媒体和输入 trace，并强制要求 `libmpv/deko3d` 后重新编译。复现 UI 卡顿、触摸事件、SOAP/player 状态不同步、播放失败时用这个包。

## CI/CD

GitHub Actions 使用和本地 Docker 构建一致的 Dockerfile 和媒体包版本。

开发包：

```bash
git push
```

任意分支 push 后会构建项目，并更新滚动预发布：

- Release 名称：`NX-Cast Continuous`
- Tag：`continuous`
- Assets：`NX-Cast.nro`、`NX-Cast-sdmc.zip`

正式发布：

```bash
git tag v0.1.0
git push origin v0.1.0
```

正式 release workflow 会强制要求 `libmpv/deko3d`，并拒绝明显异常的小体积 `NRO`。

## 目录

```text
assets/
  dlna/        复制到 SD 卡的 DLNA 运行时模板
  icon/        NRO 图标源文件
docs/          设计说明和实施规划
scripts/       构建、打包、nxlink、冒烟测试
source/
  log/
  player/
    backend/
    core/
    render/
    ui/
  protocol/
    dlna/
    http/
```

以下目录是生成物，已经被忽略：

```text
build/
dist/
sdmc/
artifacts/
logs/
```

## 文档

从 [docs/README.md](docs/README.md) 开始看。

推荐阅读顺序：

1. [docs/dmr-implementation.md](docs/dmr-implementation.md)
2. [docs/player-layer.md](docs/player-layer.md)
3. [docs/render-design.md](docs/render-design.md)
4. [docs/scpd-module.md](docs/scpd-module.md)
5. [docs/iptv-gui-plan.md](docs/iptv-gui-plan.md)

如果文档和源码冲突，以当前 `source/` 为准。
