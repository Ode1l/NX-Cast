# NX-Cast

NX-Cast 是一个运行在 Atmosphère 环境下的 Nintendo Switch 开源无线投屏接收程序。

该项目的目标是在 **不安装 Linux 的情况下**，通过 Switch 原生 Homebrew 环境实现无线媒体接收功能，例如 **DLNA 和 AirPlay 类似协议的视频接收**。

NX-Cast 基于 **devkitPro + libnx** 开发，并运行在 Atmosphère 的 homebrew 上。

---

## 项目目标

NX-Cast 的目标是：

- Atmosphère 实现
- 支持无线媒体接收
- 模块化协议架构
- 利用 Switch 硬件解码能力
- 构建可维护的开源社区项目

长期目标是为 Switch Homebrew 生态提供一个 **通用媒体接收框架**。

---

## 当前状态

项目目前处于“通用 DMR 底座已成型，完整播放器后端待补完”的阶段。

当前进度：

- 应用启动、日志系统与网络初始化已经就位。
- 发现层已具备 SSDP 响应、service-type 回复与 mDNS 探测能力。
- DLNA DMR 核心链路已基本实现：SCPD、SOAP 路由、AVTransport / RenderingControl / ConnectionManager、GENA 事件订阅，以及对应 smoke 测试。
- `player` 已成为真实状态源，`SOAP -> player -> libmpv` 控制链路已打通；`SetURI / Play / Pause / Stop / Seek / GetTransportInfo / GetPositionInfo` 的主流程已通过实机 smoke。
- `Step 2.1 / Step 2.2` 已落地：主线程前台显示权、render loop 骨架，以及 `software render + libnx framebuffer` 的最小真实出画路径均已接通。
- `ingress` 已从规则堆叠式实现收敛为 `evidence -> classify -> resource_select -> policy` 的第一版系统化流水线，并补了 `http` URL preflight、`SinkProtocolInfo` 扩展、metadata 资源选择与 `local_proxy` transport policy。
- `mp4`、`bilibili`、`mgtv`、部分 `youku/tencent/cctv` 路径已完成实机验证；`iqiyi` 仍未打通，当前更像站点请求上下文或系统媒体能力差距，而不是基础 DMR 不通。
- 当前主矛盾已从“协议是否能通”转移到两条线：
  1. 通用 DMR 完整度继续补完
  2. 完整播放器后端：真实音频输出、`OpenGL/libmpv render API` 路径、硬解码接入

当前后端路线决策：

1. 当前正式路线采用 `ao=hos + hwdec=nvtegra + OpenGL/libmpv render API`
2. `deko3d` 仍然保留为未来能力，但不再作为当前阶段立即接入目标
3. 原因不是架构否定 `deko3d`，而是当前官方 `libmpv` 工具链缺少 `mpv/render_dk3d.h`
4. 如果后续切换到自定义媒体工具链，再进入 `libuam + FFmpeg(nvtegra) + mpv(deko3d + hos-audio)` 路线

---

## 里程碑 0 启动

本阶段的目标是让 NX-Cast 能在 Atmosphère 中稳定启动，并完成后续协议开发需要的基础网络准备。

已完成内容：

- 建立基于 devkitPro/libnx 的仓库与构建脚本
- Homebrew 入口程序：初始化控制台输出和手柄输入
- 生成可部署的 `.nro/.nacp` 文件
- 通过 `socketInitializeDefault()` 启动网络栈，并输出运行时诊断信息

本地验证步骤：

1. 安装 devkitPro（含 devkitA64 与 libnx），执行 `make`。
2. 将生成的 `NX-Cast.nro` 拷贝到 SD 卡的 `/switch/nx-cast/`。
3. 保证 Switch 已连接 Wi-Fi，从 Homebrew Menu 启动 NX-Cast，查看控制台中的 `[net]` 初始化日志。
4. 看到网络初始化成功提示后，按 `+` 退出。

---

## 里程碑 1 设备发现

该里程碑增加了基于 SSDP 的 DLNA/UPnP 设备发现能力。

实现内容：

- 通过向 `239.255.255.250:1900` 发送 `M-SEARCH` 探测请求获取同局域网中的服务。
- 在控制台打印响应设备的 IP/端口以及原始响应头，并缓存 USN/ST 等元数据，方便确认兼容设备。
- 新增基础 SSDP 响应端，使 NX-Cast 可以回复来自手机/电脑的 `M-SEARCH` 请求（周期性 `NOTIFY` 广播暂未实现）。
- 引入 `network/discovery` 模块，为后续的 mDNS/AirPlay 发现逻辑提供统一入口。
- 实现基于 `_airplay._tcp.local` 的 mDNS 查询，多播请求并输出 PTR 应答结果。
- 明确目标是实现 DLNA 的 DMR 角色，播放控制与内容由第三方 DMC/DMS 提供。
- 这一阶段建立的 DLNA control 骨架，已经演进为当前正式的 `SOAP + GENA` 控制层。

验证步骤：

1. 与里程碑 0 相同方式构建并部署最新 `.nro`。
2. 确保 Switch 与 DLNA/UPnP 发送端处于同一 Wi-Fi 网络。
3. 启动 NX-Cast，观察控制台中的 `[ssdp]` 日志，确认出现响应后按 `+` 退出。

---

## 计划功能

### Phase 1
- 应用框架
- 网络模块
- 设备发现
- 状态：基本完成

### Phase 2
- DLNA Digital Media Renderer（DMR）实现
- SCPD + SOAP 控制链路
- 状态：核心能力已基本完成；`SSDP responder + service-type response`、`SOAP`、`GENA eventing`、`SinkProtocolInfo`、metadata 返回和当前兼容性底座已落地

### Phase 3
- 基础播放管线
- 设备端画面渲染
- player 入口资源选择（基于 `CurrentURIMetaData` `DIDL-Lite res/protocolInfo` 候选资源）
- Step 2.1：确定前台显示权归属，建立主线程驱动的 render loop 骨架
- Step 2.2：接入 `libmpv render API`，通过 `software render + libnx framebuffer` 打通最小视频显示路径
- Step 2.3：完善日志 UI 切换、屏幕接管与 `OpenGL/libmpv render API` 路径
- 状态：Step 1 与 Step 2.1 / 2.2 已落地，player 入口资源选择也已落地第一版；下一步转向真实音频输出、`OpenGL/libmpv render API` 与硬解码

### Phase 4
- 硬件解码支持
- `hwdec=nvtegra`
- 状态：已进入实现与验证阶段；当前不再把 `deko3d` 作为这一阶段前置条件

### Phase 5
- AirPlay 类视频投屏

### Phase 6
- DMP 扩展
- 源点播节目浏览与播放
- 基于来源的节目列表、详情页与点播入口
- 视目标范围决定是否引入 source-native adapter

### Phase 7
- UI 与配置界面
- 添加到桌面（快捷入口）

### Optional Phase
- 自定义 `mpv` 工具链
- `deko3d` 渲染后端
- `render_dk3d` / `libuam` 路线
- 基于 sysmodule 的后台常驻服务
- 脱离普通前台 homebrew 生命周期后的挂起/持续发现与监听

---

## 系统架构

NX-Cast 采用分层架构：

```text
应用入口(main)
│
协议层
(DLNA / AirPlay 占位)
│
控制与描述
(SSDP / SCPD / SOAP / GENA)
│
player
(core / ingress / backend / render)
│
平台与依赖
(libnx / libmpv / 网络 / 后续 deko3d)
```

---

## 项目目录结构

```text
nx-cast
│
├── docs
├── scripts
├── source
│   ├── log
│   ├── player
│   │   ├── core
│   │   ├── ingress
│   │   ├── backend
│   │   └── render
│   ├── protocol
│   │   ├── airplay
│   │   ├── dlna
│   │   │   ├── discovery
│   │   │   ├── description
│   │   │   └── control
│   │   └── http
│   └── main.c
├── xml
├── README.md / README_CN.md
└── ROADMAP.md
```

建议阅读顺序：

1. [Player层设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/Player层设计.md)
2. [源兼容性.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/源兼容性.md)
3. [DMR实现细节.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/DMR实现细节.md)
4. [render设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/render设计.md)

---

## 构建环境

需要安装 Switch Homebrew 开发工具链：

- devkitPro
- devkitA64
- libnx

编译：

```text
make
 生成 `.nro` 文件。
```

### Docker 构建（可选）

如果你不想在本机安装 devkitPro，可用 Docker 进行可复现构建：

```text
./scripts/docker_build.sh
```

等价的 docker compose 命令：

```text
docker compose build nx-cast-build
docker compose run --rm nx-cast-build
```

说明：

- 设置 `NO_CLEAN=1` 可跳过 clean（`NO_CLEAN=1 ./scripts/docker_build.sh`）。
- Docker 仅负责编译；运行与投屏联调仍需要真实 Switch 设备。

---

## 运行

将 `.nro` 放入：

```text
/switch/nx-cast/
通过 Homebrew Menu 启动。
```

---

## 贡献

欢迎贡献代码。

提交 PR 前请阅读 `CONTRIBUTING.md`。

当前需要帮助的方向：

- 协议实现
- 媒体处理优化
- 硬件解码
- UI 改进
- 文档完善

---

## 发布准备

为了将 NX-Cast 正式发布到 switchbrew，除了核心功能外还需要补齐以下内容：

- 许可证信息：保持 GPLv3 LICENSE，并在 `README`、发布页面和 `.nro/.nacp` 元数据中注明，同时列出依赖库和素材的授权情况。
- 文档集合：开发环境搭建、模块/协议设计文档、编码规范、贡献流程、可复现的构建与测试指引。
- 发布元数据：语义化版本号、更新日志、Release Note、`.nro/.nacp` 的应用名称/作者/描述，以及可选的截图或演示视频。
- 合规说明：声明不包含受版权保护的固件/密钥，列出运行时依赖与安全注意事项。
- 社区支持：Issue/PR 模板、贡献者行为准则、维护者联系方式以及必要的审核流程说明。

---

## CI/CD 期望

为了提高可信度，建议提供最基本的自动化流程：

- 持续集成：在每个 PR 上运行格式检查、静态分析、`make` 构建 `.nro` 和现有测试。
- 可选的许可证扫描与安全检查，避免引入不符合要求的依赖。
- 发布流程自动化：在 CI 中打包产物并生成 release note，减少人工步骤。

当前工作流：

- `CI`：`.github/workflows/ci.yml`（push/PR 自动构建并上传产物）。
- `Release`：`.github/workflows/release.yml`（推送 `v*` 标签时自动创建 GitHub Release，并上传 `NX-Cast.nro`）。

标签发布示例：

```text
git tag v0.1.0
git push origin v0.1.0
```

---

## 致谢

NX-Cast 由本项目独立实现，但在协议架构、播放器分层以及 Switch 媒体后端方向上参考了以下开源项目的公开设计与源码思路：

- [gmrender-resurrect](https://github.com/hzeller/gmrender-resurrect)：提供了 DLNA/UPnP DMR 服务建模、SCPD/SOAP/服务拆分以及渲染端控制流程方面的参考。
- [pPlay](https://github.com/Cpasjuste/pplay)：提供了 `player` facade 设计与 `libmpv` 后端分层思路的参考。
- [NXMP](https://github.com/proconsule/nxmp)：提供了 Switch 平台媒体后端方向的参考，包括 `libmpv`、`hos` 音频、`deko3d` 与硬解码接入思路。
- [PlayerNX](https://github.com/XorTroll/PlayerNX)：提供了最小 FFmpeg 解码链路的参考，用于软件解码验证与研究。

感谢这些项目的维护者与贡献者。

---

## License

NX-Cast 采用 GNU GPLv3 许可证。完整条款见 `LICENSE`。
Copyright (c) 2026 Ode1l。

---

## 免责声明

NX-Cast 为独立开源项目，与 Nintendo 无任何官方关系。
