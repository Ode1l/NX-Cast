# NX-Cast

`NX-Cast` 是运行在 Atmosphere Homebrew 环境下的 Nintendo Switch DLNA 接收器和 IPTV 播放器。

它既可以作为通用 `DLNA DMR`，接收手机、桌面播放器和电视应用发送的媒体 URL，也可以独立浏览和播放本地或远程 M3U IPTV 源。两条播放路径共用同一个支持硬解的 `libmpv` 播放会话。

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
- 本地/远程 M3U 源管理、SD 缓存和 IPTV 直链输入
- 频道分组、搜索、收藏、最近播放、台标缓存和 XMLTV 当前/下一节目
- 手柄和触摸播放器 overlay
- Docker 和 GitHub Actions 发布构建

项目仍然是实验性 Switch homebrew。当前开发重点是 DLNA 兼容性、播放稳定性和播放器 UI。

## 当前不做什么

`NX-Cast` 当前不是：

- DLNA 媒体服务器，也就是 `DMS`
- DLNA 控制器，也就是 `DMC`
- AirPlay 接收器
- iQiyi、MangoTV、CCTV、Bilibili 等平台的站点原生客户端或频道提供方
- DRM 绕过或站点登录实现

当前播放链保持很薄：DLNA 只提供 URL，后续探测、网络请求、demux、decode 和播放交给 `libmpv/FFmpeg`。

## IPTV

IPTV 已经是正式发布功能，不需要使用单独的实验构建。NX-Cast 可以导入本地或远程 M3U/M3U8 播放列表，区分频道列表和直接 HLS 流，把远程源缓存到 SD 卡，并且可以从主页或播放画面上方打开频道浏览器。

IPTV 浏览器支持播放列表分组、搜索、收藏、最近播放、持久化源管理、异步台标缓存，以及 plain/gzip XMLTV 当前和下一节目信息。用户需要提供自己有权访问的播放列表和可选节目单地址；NX-Cast 不提供付费频道权限，也不会绕过 DRM。

支持格式、SD 卡路径、源配置、控制方式和当前限制见 [docs/iptv.md](docs/iptv.md)。

## 安装

优先使用 GitHub Release 里的 SD 卡发布包：

1. 下载 `NX-Cast-sdmc.zip`
2. 解压到 Switch SD 卡根目录
3. 从 `hbmenu` 启动 `switch/NX-Cast/NX-Cast.nro`

发布包结构：

```text
switch/
  NX-Cast/
    NX-Cast.nro
    dlna/
    fonts/
    iptv/
```

`NX-Cast-sdmc.zip` 已经按 SD 卡目录排好结构，直接解压到 SD 卡根目录即可。不要多套一层 `NX-Cast-sdmc/switch/...` 目录。

`switch/NX-Cast/dlna/` 里是运行时需要的 DLNA XML、CSV、HTML 和图标资源。`switch/NX-Cast/fonts/` 里是 UI 字体和许可证说明。本地 `.m3u` 或 `.m3u8` 播放列表放入 `switch/NX-Cast/iptv/`。

完整安装和排错说明见 [docs/install.md](docs/install.md)。

## 控制

空闲时，软件显示主页：基础投屏教程、运行状态和最后一条错误。完整日志历史仍保留给调试使用，但不会作为发布版前台 UI 展示。

主页 IPTV 控制：

- `X` 或任一摇杆按下：打开 IPTV；进入 IPTV 后使用 `X` 切换频道与源管理页面
- `A`：从主页返回正在播放的视频，或播放当前选中的频道
- `Up` / `Down` 或任一摇杆纵向：逐项选择频道
- `Left` / `Right`、任一摇杆横向或 `L` / `R`：频道列表翻页
- `Y`：收藏频道、添加远程源；在主页刷新全部源
- `ZL` / `ZR`：切换频道筛选；源管理页用 `ZR` 配置 XMLTV
- `L3` / `R3`：搜索频道 / 清除搜索
- `-`：输入媒体或 M3U URL；播放列表会导入频道页，不再被当成单个视频直接播放
- `B`：关闭频道列表
- 触摸：点击频道行进行选择，再次点击或点击 `PLAY CHANNEL` 播放；滑动列表或点击屏幕翻页箭头进行翻页

NX-Cast 会合并读取所有已连接的标准手柄。单只横握 Joy-Con 可以用摇杆浏览和打开 IPTV，使用 `SR` 确认、`SL` 返回；双 Joy-Con、掌机模式、Pro Controller 和触摸屏也都能各自独立完成选台。

视频播放时：

- `A`：播放 / 暂停
- `B`：返回主页，但不停止当前播放
- `X` 或任一摇杆按下：在当前视频上打开 IPTV 频道菜单；`A` 换台，`B` 关闭菜单
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
  -> iptv
       -> 本地/远程 M3U 缓存与分类
       -> 频道筛选、收藏、台标和 XMLTV EPG
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
make TRACE_MEDIA=1 TRACE_INPUT=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j2
```

Trace 是按需启用的构建变量，不是默认构建模式。复现 UI 卡顿、触摸事件、SOAP/player 状态不同步、播放失败时再打开。

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
  iptv/        IPTV 源示例和随包预设
docs/          设计说明和实施规划
scripts/       构建、打包、nxlink、冒烟测试
source/
  iptv/
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
5. [docs/iptv.md](docs/iptv.md)
6. [docs/iptv-gui-plan.md](docs/iptv-gui-plan.md)

如果文档和源码冲突，以当前 `source/` 为准。
