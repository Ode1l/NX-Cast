# DMR 实现细节

本文档描述 `NX-Cast` 作为 `DLNA Digital Media Renderer` 的当前实现，不再保留早期“未来计划版”的表述。

---

## 1. 当前角色

`NX-Cast` 当前实现的是：

1. `DMR`：Digital Media Renderer

它的职责是：

1. 被发现
2. 提供 `device.xml` 与 `SCPD`
3. 接收 `SOAP` 控制
4. 主动拉取媒体 URL
5. 进行本地播放
6. 通过查询和事件把状态同步给控制端

它当前不实现：

1. `DMC` 控制端
2. `DMS` 媒体目录浏览
3. AirPlay 主链

---

## 2. 当前完整链路

当前 DMR 主链已经是：

```text
SSDP discover
  -> device.xml / SCPD
  -> SOAP control
  -> player ingress
  -> player core
  -> libmpv backend
  -> render/frontend
  -> SOAP query + GENA notify
```

按模块拆开是：

1. 发现层：[ssdp.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/discovery/ssdp.c)
2. 描述层：[scpd.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/description/scpd.c)
3. 控制层：[soap_server.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/soap_server.c)
4. 事件层：[event_server.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/event_server.c)
5. 播放入口：[ingress.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/ingress/ingress.c)
6. 播放核心：[session.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/core/session.c)
7. 真实后端：[libmpv.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/backend/libmpv.c)

---

## 3. 当前分层边界

### 3.1 发现层

负责：

1. 回复 `M-SEARCH`
2. 提供 `LOCATION`
3. 回复 `device` 与 `service-type` 查询

当前未做：

1. `SSDP NOTIFY ssdp:alive/byebye`

### 3.2 描述层

负责：

1. `device.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`

当前已经包含：

1. `eventSubURL`
2. `LastChange`
3. `GetCurrentTransportActions`

### 3.3 控制层

负责：

1. `SOAP` 解析
2. action 路由
3. 参数校验
4. 状态查询与动作映射

当前 action 范围：

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo / GetMediaInfo / GetPositionInfo / GetCurrentTransportActions`
4. `GetVolume / SetVolume / GetMute / SetMute`
5. `GetProtocolInfo / GetCurrentConnectionIDs / GetCurrentConnectionInfo`

### 3.4 事件层

负责：

1. `SUBSCRIBE / UNSUBSCRIBE`
2. `NOTIFY`
3. `LastChange`

当前意义：

1. 保证 DMR 事件层不是空壳
2. 与 `SOAP` 查询一起组成较完整的控制兼容面

### 3.5 Player 层

负责：

1. `CurrentURI + CurrentURIMetaData` 解析
2. 资源分类、选择和策略注入
3. 真实播放命令执行
4. 真实状态和事件输出

---

## 4. 当前已经补完的 DMR 能力

从系统性角度看，当前已补完的关键项有：

1. `SOAP -> player -> libmpv` 主链
2. `player` 作为单一真实状态源
3. `SOAP` 动态 metadata 返回
4. `CurrentTransportActions`
5. `GENA / LastChange`
6. `SinkProtocolInfo` 扩展
7. `CurrentURIMetaData` 资源选择第一版
8. URL preflight：`HEAD / GET fallback / redirect / Accept-Ranges / Content-Type`
9. `local_proxy` 与混合源分类

这意味着：

1. 当前的主要问题不再是“DLNA 主链没通”
2. 而是通用 DMR 完整度、真实媒体后端和复杂源兼容

---

## 5. 当前系统对外暴露的真实能力

### 5.1 发现

已经能让控制端：

1. 搜到设备
2. 读取描述
3. 按 `device` 或 `service-type` 把我们识别成标准 `MediaRenderer`

### 5.2 描述与控制

已经能让控制端：

1. 调 `SetURI / Play / Pause / Stop / Seek`
2. 读 `TransportInfo / MediaInfo / PositionInfo`
3. 读写音量和静音
4. 读取 `SinkProtocolInfo`

### 5.3 播放

当前真实媒体路径已经能做到：

1. `mp4` 基线播放
2. `HLS` 基线播放
3. 部分 vendor-sensitive 来源的实机出画

当前边界仍然是：

1. 音频输出未完成
2. 当前正式后端不再以 `deko3d` 作为立即目标；当前阶段采用 `ao=hos + hwdec=nvtegra + OpenGL/libmpv render API`
3. 硬解码未接入

---

## 6. 当前最接近商用品差距的部分

与成熟 DMR/电视盒子相比，当前差距主要不在“有没有 SOAP”，而在：

1. 通用媒体探测是否足够完整
2. `SinkProtocolInfo` 与 metadata 选择是否足够成熟
3. 混合源 / 本地代理 transport 是否有独立策略
4. 完整播放器后端是否足够平台化

这也是为什么：

1. `bilibili / mgtv` 已经有明显进展
2. `iqiyi` 仍然不稳定
3. 本地代理 HLS 仍然会暴露源链路极限

---

## 7. 当前剩余工作

### 7.1 DMR 侧

当前真正还没做的 DMR 协议项已经不多：

1. `SSDP NOTIFY ssdp:alive/byebye`
2. 继续扩完整的 `SinkProtocolInfo`
3. 继续完善 `LastChange` 内容和控制端兼容矩阵

### 7.2 Player / backend 侧

接下来更重要的是：

1. 真实音频输出
2. `OpenGL/libmpv render API`
3. `hwdec=nvtegra`
4. `deko3d` 作为未来能力
4. 更强的 transport/media preflight

---

## 8. 结论

一句话总结：

`NX-Cast` 当前已经不是“只会回 XML 的半成品 DMR”，而是拥有发现、描述、控制、事件和真实播放主链的通用 DMR 雏形；后续重点应转向完整播放器后端和更高完整度的通用媒体能力，而不是再回到基础 SOAP bring-up。
