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

项目目前处于原型快速推进阶段。

当前进度：

- 应用启动、日志系统与网络初始化已经就位。
- 发现层已具备 SSDP 响应与 mDNS 探测能力。
- DLNA DMR 核心链路已基本实现：SCPD、SOAP 路由、AVTransport / RenderingControl / ConnectionManager，以及对应 smoke 测试均已接通。
- 当前剩余的 DLNA 工作主要是 optional 能力，例如 `NOTIFY ssdp:alive/byebye` 和更完整的事件通知。
- 为了接近商用电视盒子的投屏兼容性，后续还需要补两项关键能力：协议侧的 `ConnectionManager/SinkProtocolInfo` 输入能力宣告扩展，以及 player 入口侧基于 `CurrentURIMetaData` `DIDL-Lite res/protocolInfo` 候选资源的接收端选择。
- 下一阶段的重点是协议之后的真实播放与画面渲染。

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
- 新增 DLNA 控制占位模块，读取缓存结果并记录后续将建立控制会话的设备。

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
- 状态：核心能力基本完成，剩余 optional 的 `NOTIFY ssdp:alive/byebye`、更完整事件通知能力，以及协议侧的 `ConnectionManager/SinkProtocolInfo` 扩展

### Phase 3
- 基础播放管线
- 设备端画面渲染
- player 入口资源选择（基于 `CurrentURIMetaData` `DIDL-Lite res/protocolInfo` 候选资源）

### Phase 4
- 硬件解码支持

### Phase 5
- AirPlay 类视频投屏

### Phase 6
- UI 与配置界面
- 添加到桌面（快捷入口）

### Optional Phase
- 基于 sysmodule 的后台常驻服务
- 脱离普通前台 homebrew 生命周期后的挂起/持续发现与监听

---

## 系统架构

NX-Cast 采用分层架构：

```text
应用层
│
协议层
(AirPlay / DLNA)
│
媒体处理
│
解码层
│
渲染层
│
平台层 (libnx)
```

---

## 项目目录结构

```text
nx-cast
│
├── docs
│
├── src
│ ├── app
│ ├── network
│ ├── protocol
│ ├── media
│ ├── decoder
│ └── render
│
├── include
│
├── examples
│
└── tests
```

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
