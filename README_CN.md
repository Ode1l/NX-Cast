# NX-Cast

`NX-Cast` 是运行在 Atmosphère Homebrew 环境下的 Nintendo Switch 开源媒体接收器。

当前主线目标不是“做某一个站点的专用客户端”，而是把 Switch 上的 **通用 DLNA DMR 底座** 做完整，再在这个底座之上逐步增强混合源、站点策略和完整播放器后端。

## 当前状态

项目已经完成并验证过这些基础能力：

- `SSDP` 发现、`device.xml`、`SCPD`
- `SOAP` 控制链：`SetAVTransportURI / Play / Pause / Stop / Seek`
- `GENA` 事件订阅与 `LastChange`
- `player` 统一状态源、owner thread、命令队列
- `ingress` 标准输入建模与资源选择
- `libmpv` 后端
- `ao=hos`
- `OpenGL/libmpv render API`

当前更准确的阶段是：

1. 通用 `DMR` 底座已成立
2. 标准输入建模正在收口
3. 混合源与 transport 稳定性仍在迭代
4. `hwdec=nvtegra` 仍受当前官方工具链限制

## 当前设计原则

项目当前按这几条原则推进：

1. 结构重构与行为变化分开做
2. 标准输入先正确建模，再谈播放策略
3. `vendor` 只做加法，不覆盖标准解析结果
4. 协议层不直接做站点兼容逻辑
5. `player` 是唯一真实播放状态源
6. `SOAP`、`LastChange`、兼容查询都读取同一份协议观察状态

一句话说：

**先把“这是什么源”建模清楚，再决定“怎么播它”。**

## 当前架构

```text
main
  -> protocol/dlna
       -> discovery (SSDP)
       -> description (device.xml / SCPD)
       -> control (SOAP / GENA / protocol_state)
  -> player
       -> core (owner thread / queue / snapshot)
       -> ingress (evidence -> model -> resource_select -> http_probe -> media -> policy)
       -> backend (libmpv / mock)
       -> render (view / frontend)
```

当前两条关键状态线：

1. `player` 维护真实播放状态
2. `protocol_state` 维护协议观察状态

## ingress 当前流水线

当前 `player/ingress` 已经不是“边解析边改最终对象”的旧模式，而是固定成：

```text
CurrentURI + CurrentURIMetaData + request headers
  -> evidence
  -> IngressModel
  -> metadata resource selection
  -> http probe / preflight
  -> PlayerMedia
  -> policy
```

它回答的问题分成两层：

1. 解析层：这是什么源
2. 策略层：怎样更稳地打开它

当前显式 transport 分类：

- `http-file`
- `hls-direct`
- `hls-local-proxy`
- `hls-gateway`

## 当前后端路线

当前正式后端路线是：

1. `ao=hos`
2. `OpenGL/libmpv render API`
3. `libmpv` 继续作为播放器核心

需要特别区分：

- `OpenGL` / `deko3d` 是渲染路径
- `hwdec=nvtegra` 是解码路径

当前结论：

1. `hos-audio + OpenGL` 已接通
2. `hwdec=nvtegra` 已在代码路径中考虑，但当前官方 `dkp` `libmpv` 工具链下还没有真正成为可用的 explicit backend
3. `deko3d` 仍然保留为未来能力，不是当前默认路线

## 当前工作重点

当前优先级不是继续堆单点站点 hack，而是：

1. 完成标准输入建模
2. 完成通用 `DMR` 兼容面
3. 稳定 `local_proxy` 与 `HLS gateway` 这类 transport
4. 继续完善控制端位置同步与互操作
5. 在工具链成熟后再推进 `nvtegra` 和未来 `deko3d`

## 目录

```text
source/
  main.c
  log/
  player/
    core/
    ingress/
    backend/
    render/
  protocol/
    dlna/
      discovery/
      description/
      control/
    http/
```

## 推荐阅读顺序

1. [docs/Player层设计.md](docs/Player层设计.md)
2. [docs/DMR实现细节.md](docs/DMR实现细节.md)
3. [docs/源兼容性.md](docs/源兼容性.md)
4. [docs/render设计.md](docs/render设计.md)
5. [ROADMAP.md](ROADMAP.md)

## 构建

需要：

- `devkitPro`
- `devkitA64`
- `libnx`

构建：

```bash
make
```

输出：

- `NX-Cast.nro`

## 当前文档说明

仓库文档已经按当前状态重写，不再保留“早期未来计划版”的表述。  
如果文档和代码冲突，以 `source/` 当前实现为准，并应继续更新文档而不是保留过时描述。
