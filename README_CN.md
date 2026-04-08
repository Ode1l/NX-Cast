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
- `OpenGL/libmpv render API`

当前更准确的阶段是：

1. 通用 `DMR` 底座已成立
2. 协议状态与运行时播放状态已经围绕同一播放会话收口
3. 直接 `URL -> libmpv` 的播放链已经落地
4. `hwdec=nvtegra` 仍受当前官方工具链限制

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
- `idle-active`
- `paused-for-cache`
- `seeking`
- EOF / error 状态

项目当前已经不再保留独立的预处理播放层。

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
2. `hwdec=nvtegra` 当前还不能作为官方 `dkp` 工具链下的稳定基线
3. `deko3d` 仍然保留为未来能力，不是当前默认路线

## 当前工作重点

当前优先级是：

1. 继续补通用 `DMR` 兼容面
2. 提高协议状态与控制端进度同步的准确性
3. 保持模板化描述层与真实实现一致
4. 在真实 URL 和真实控制端上继续加固播放稳定性
5. 在工具链成熟后再推进 `nvtegra` 和未来 `deko3d`

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
romfs/
  dlna/
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

构建：

```bash
make
```

输出：

- `NX-Cast.nro`

## 当前文档说明

仓库文档按当前实现维护，不再保留已经删除的旧设计表述。  
如果文档和代码冲突，以 `source/` 当前实现为准，并应继续更新文档而不是保留过时描述。
