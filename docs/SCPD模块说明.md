# SCPD 模块说明

本文档描述 `source/protocol/dlna/description/` 当前的职责和边界。

## 1. 模块定位

描述层当前负责：

1. `Description.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`
5. `SinkProtocolInfo.csv`

它不负责：

1. 发现
2. 播放
3. 动作执行
4. 运行时状态维护

一句话：

描述层负责把当前设备能力以模板化资源对外输出清楚。

## 2. 当前实现方式

当前描述层不是写死在代码里的长字符串，而是：

1. 本地模板文件放在 `romfs/dlna/`
2. 软件运行时按请求流式读取模板
3. 在输出过程中替换设备信息和 URL

这套资源当前包括：

1. `Description.xml`
2. `AVTransport.xml`
3. `RenderingControl.xml`
4. `ConnectionManager.xml`
5. `SinkProtocolInfo.csv`

## 3. 当前设备描述

当前设备描述已声明：

1. `MediaRenderer:1`
2. `AVTransport`
3. `RenderingControl`
4. `ConnectionManager`
5. 三个服务的 `SCPDURL / controlURL / eventSubURL`

## 4. 当前服务描述

### 4.1 AVTransport

当前宣告：

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`
7. `LastChange`

### 4.2 RenderingControl

当前宣告：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness`
4. `LastChange`

### 4.3 ConnectionManager

当前宣告：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## 5. 当前原则

描述层当前保持这些原则：

1. 能力描述尽量完整
2. 与真实实现保持一致
3. 模板资源尽量简单可维护
4. 运行时动态值只在输出时替换

## 6. SinkProtocolInfo

`SinkProtocolInfo` 当前来自独立 CSV 模板资源。

当前目标不是一次性伪装“全能设备”，而是：

1. 对外声明当前实际能力面
2. 与当前 `libmpv` 播放能力保持一致
3. 随真实兼容能力逐步扩充

## 7. 当前文档位置

如果要继续看：

1. renderer 如何同步真实播放状态
2. 为什么 `LastChange` 和查询动作都依赖同一份协议状态

请继续阅读：

1. [DMR实现细节.md](DMR实现细节.md)
2. [Player层设计.md](Player层设计.md)
