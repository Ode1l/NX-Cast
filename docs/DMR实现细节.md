# DMR 实现细节

本文档描述 `NX-Cast` 当前作为 `DLNA Digital Media Renderer` 的真实实现边界。

## 1. 当前角色

`NX-Cast` 当前实现的是：

1. `DMR`

它当前负责：

1. 被发现
2. 提供设备描述与服务描述
3. 接收 `SOAP` 控制
4. 订阅和发送 `GENA` 事件
5. 拉取控制端给出的媒体 URL
6. 进行本地播放

它当前不负责：

1. `DMC`
2. `DMS`
3. AirPlay 主链
4. 站点原生浏览

## 2. 当前主链

```text
SSDP
  -> Description.xml / SCPD
  -> SOAP
  -> renderer
  -> backend/libmpv
  -> runtime events / properties
  -> protocol_state
  -> SOAP query / GENA notify
```

对应模块：

1. `source/protocol/dlna/discovery/`
2. `source/protocol/dlna/description/`
3. `source/protocol/dlna/control/`
4. `source/player/core/`
5. `source/player/backend/`
6. `source/player/render/`
7. `assets/dlna/` -> `sdmc:/switch/NX-Cast/dlna/`

## 3. 当前协议层原则

当前 `DLNA` 底座按这些原则实现：

1. `SOAP` 只做协议解析、参数校验、动作映射
2. 协议层不直接做站点兼容逻辑
3. 协议观察状态只有一份真源
4. `SOAP`、`LastChange`、兼容查询都读同一份协议状态

这一份共享状态现在由：

- [protocol_state.h](../source/protocol/dlna/control/protocol_state.h)
- [protocol_state.c](../source/protocol/dlna/control/protocol_state.c)

负责维护。

## 4. 当前控制层能力

### 4.1 AVTransport

当前已实现：

1. `SetAVTransportURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `Seek`
6. `GetTransportInfo`
7. `GetMediaInfo`
8. `GetPositionInfo`
9. `GetCurrentTransportActions`

### 4.2 RenderingControl

当前已实现：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness` 兼容 stub

### 4.3 ConnectionManager

当前已实现：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## 5. 当前状态来源

当前状态线分成两层：

1. renderer
   - 真实播放状态
   - 位置、时长、音量、静音、seekable
   - 当前媒体 URI
2. `protocol_state`
   - 协议观察状态
   - `TransportState`
   - `TransportStatus`
   - URI 与 metadata
   - `LastChange` 对外输出

这意味着：

1. 协议层不再自己拼一份运行时状态
2. 运行时状态主要来自 renderer 对 `libmpv` 的观察结果

## 6. 当前与 renderer 的边界

协议层与 renderer 之间只通过：

1. 命令
2. `RendererSnapshot`
3. `RendererEvent`

交互。

协议层不应直接知道：

1. backend 内部细节
2. render 路径细节
3. `libmpv` 之外的具体实现细节

## 7. 当前工作重点

当前 `DMR` 主线已不再是“能不能收 `SOAP`”，而是：

1. 继续补通用 `DMR` 兼容面
2. 提高 `LastChange` 与进度同步的准确性
3. 保持描述层和真实能力一致
4. 在真实控制端上持续改善互操作

## 8. 相关文档

1. [Player层设计.md](Player层设计.md)
2. [SOAP模块说明.md](SOAP模块说明.md)
3. [SCPD模块说明.md](SCPD模块说明.md)
