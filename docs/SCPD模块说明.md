# SCPD 模块说明

本文档描述 `source/protocol/dlna/description/` 当前的职责和边界。

## 1. 模块定位

`SCPD` 层负责：

1. `device.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`

它不负责：

1. 发现
2. 播放
3. 动作执行
4. 运行时状态维护

一句话：

`SCPD` 负责把当前设备能力说清楚。

## 2. 当前设备描述

当前 `device.xml` 已声明：

1. `MediaRenderer:1`
2. `AVTransport`
3. `RenderingControl`
4. `ConnectionManager`
5. 三个服务的 `SCPDURL / controlURL / eventSubURL`

## 3. 当前服务描述

### 3.1 AVTransport

当前宣告：

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`
7. `LastChange`

### 3.2 RenderingControl

当前宣告：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness`
4. `LastChange`

### 3.3 ConnectionManager

当前宣告：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## 4. 当前原则

`SCPD` 当前保持这些原则：

1. 能力描述尽量完整
2. 与真实实现保持一致
3. 静态结构尽量简单
4. 运行时动态值只保留必要字段

## 5. SinkProtocolInfo

`SinkProtocolInfo` 当前已经不再是极窄声明。

当前目标不是一次性伪装“全能设备”，而是：

1. 对外声明当前实际能力面
2. 逐步扩充常见音视频类型
3. 与真实 `ingress + backend` 能力保持一致

## 6. 当前文档位置

如果要看：

1. 为什么 `SinkProtocolInfo` 要逐步扩表
2. 为什么 metadata 与事件面会影响控制端兼容

请继续阅读：

1. [DMR实现细节.md](DMR实现细节.md)
2. [源兼容性.md](源兼容性.md)
