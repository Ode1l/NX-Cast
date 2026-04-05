# SCPD 模块说明

本文档描述 `source/protocol/dlna/description/scpd.c` 当前的职责和边界。

---

## 1. 模块定位

`SCPD` 模块属于 `DLNA description layer`，负责：

1. `device.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`

它不负责：

1. 发现
2. 播放
3. SOAP 动作执行
4. 真实状态维护

一句话：

`SCPD` 负责把当前设备能力对外说清楚。

---

## 2. 当前对外描述内容

### 2.1 device.xml

当前 `device.xml` 已经声明：

1. `MediaRenderer:1`
2. `AVTransport`
3. `RenderingControl`
4. `ConnectionManager`
5. 三个服务的 `SCPDURL / controlURL / eventSubURL`

对应实现：

1. [scpd.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/description/scpd.c)

### 2.2 AVTransport SCPD

当前已包含：

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`
7. `LastChange`

### 2.3 RenderingControl SCPD

当前已包含：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `LastChange`

### 2.4 ConnectionManager SCPD

当前已包含：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`
4. `SinkProtocolInfo`

---

## 3. 当前动态与静态边界

### 3.1 动态部分

运行时注入：

1. `friendlyName`
2. `manufacturer`
3. `modelName`
4. `UDN`

### 3.2 静态部分

内嵌到代码中的固定结构：

1. 三个服务声明
2. `actionList`
3. `serviceStateTable`
4. URL 拓扑

这样做的原因：

1. 当前更需要稳定性和可审计性
2. 不需要在运行时动态拼装一整套 XML 结构

---

## 4. 当前与控制层的关系

`SCPD` 现在已经不再是“只够把控制点骗进 SOAP”。

它与控制层的真实关系是：

1. `SCPD` 宣告 `GetCurrentTransportActions / LastChange / eventSubURL`
2. `SOAP` 和 `GENA` 实际实现这些能力
3. 控制端能按标准路径建立更完整的兼容模型

这也是为什么：

1. 早期空 metadata/空事件的时期，很多控制端交互不完整
2. 当前补齐描述与返回后，互操作性明显提高

---

## 5. SinkProtocolInfo 的位置

`SinkProtocolInfo` 属于 `ConnectionManager` 的描述与能力宣告，不属于 player 层。

当前状态：

1. 已经从早期极窄声明扩到更接近当前真实能力
2. 包含常见 `mp4 / m4v / mpeg / ts / hls / flv / 常见音频`
3. 仍会继续随着真实能力演进

因此：

1. 这项能力已经不再是“待设计占位”
2. 当前工作更偏向继续扩表和实测校准

---

## 6. 当前目录与运行时关系

需要注意两个事实：

1. 运行时真正使用的是 [scpd.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/description/scpd.c) 里的内嵌 XML
2. 仓库中的 [xml/](/Users/ode1l/Documents/VSCode/NX-Cast/xml) 更适合作为参考样本，不是当前主运行路径

---

## 7. 当前仍未完成的部分

描述层现在不是主短板，但还剩两类后续工作：

1. 随着真实能力继续更新 `SinkProtocolInfo`
2. 持续校准 `action/stateVariable` 与控制端实际兼容矩阵

---

## 8. 当前结论

一句话总结：

`SCPD` 模块当前已经从“静态占位描述”升级为与 `SOAP + GENA + SinkProtocolInfo` 一致的正式描述层，后续只需做增量校准，不再是大规模重构对象。
