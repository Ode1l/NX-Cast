# DMR 实现细节（面向 NX-Cast）

本文档定义 `NX-Cast` 作为 `DLNA DMR` 的整体实现方式，按本项目采用的职责边界、数据流和状态流来描述。

---

## 1. 角色定义

`NX-Cast` 的目标角色是：

1. `DMR`：Digital Media Renderer

典型交互方：

1. `DMC`：Digital Media Controller
   例如手机、平板、电脑上的投屏控制端
2. `DMS`：Digital Media Server
   例如媒体服务器、NAS、HTTP 媒体源

`NX-Cast` 本身不做控制端，也不做媒体目录浏览器。  
它的职责是：

1. 被发现
2. 被控制
3. 接收媒体 URI
4. 主动拉取并播放该 URI

---

## 2. 总体链路

`DMR` 的完整链路可以概括为：

`发现 -> 描述 -> 控制 -> 拉流 -> 播放 -> 事件同步`

展开后是：

1. `SSDP` 让控制端发现设备
2. 控制端通过 `LOCATION` 访问 `device.xml`
3. 控制端继续读取各服务的 `SCPD`
4. 控制端对 `controlURL` 发送 `SOAP Action`
5. `NX-Cast` 通过 `player` 拉取媒体 URI
6. 本地进行播放、渲染、音频输出
7. 状态变化再通过查询或事件同步回控制端

---

## 3. 采用的架构模式

### 3.1 分层模式

整个 DMR 采用严格分层：

1. 发现层：`SSDP`
2. 描述层：`device.xml + SCPD`
3. 控制层：`SOAP`
4. 播放层：`player`
5. 事件层：`GENA / LastChange`（后续）

规则：

1. 上层不直接替代下层职责
2. 每层只暴露必要接口
3. 每层可单独调试与冒烟测试

### 3.2 控制面与数据面分离模式

DMR 必须明确区分两类流量：

1. 控制面
   包括 `SSDP / HTTP XML / SOAP`
2. 数据面
   媒体 URI 的真实拉流与播放

这两个面不能混在一起设计。

控制面只负责：

1. 被发现
2. 告诉别人“我支持什么”
3. 接收播放命令

数据面只负责：

1. 访问媒体 URI
2. 获取媒体数据
3. 解码、渲染、输出

### 3.3 Pull Model

DMR 采用“接收 URI，由设备主动拉流”的模型。

这意味着：

1. 手机通常不是直接把媒体数据推给 Switch
2. 手机通过 `SetAVTransportURI` 告诉设备媒体地址
3. 设备在 `Play` 后主动对该 URI 发起请求

这是整个 `DMR` 的核心实现前提。

### 3.4 单一状态源模式

整个系统的真实播放状态只能有一份。

建议状态来源：

1. 第一阶段：`SOAP runtime state + player mock`
2. 后续阶段：`player`

不能接受的设计：

1. `SOAP` 自己维护一套状态
2. `player` 再维护一套状态
3. UI 再维护一套状态

正确方式：

1. `player` 是真实状态源
2. `SOAP` 只是状态映射层
3. UI 和本地输入也读取同一份状态

### 3.5 增量实现模式

DMR 不应一开始追求全量协议实现。

正确顺序是：

1. 先把链路打通
2. 再补动作覆盖
3. 再补事件通知
4. 最后做兼容性和体验增强

---

## 4. 模块职责

### 4.1 发现层

职责：

1. 监听 `M-SEARCH`
2. 响应匹配的 `ST`
3. 提供 `LOCATION`
4. 可选发送 `NOTIFY`

输出给上层的信息：

1. 当前设备可被发现
2. 当前 `LOCATION` 地址可用

### 4.2 描述层

职责：

1. 提供 `device.xml`
2. 提供各服务 `SCPD`
3. 对外声明支持的服务、动作和 URL

描述层不负责：

1. 真正执行动作
2. 维护播放状态

### 4.3 控制层

职责：

1. 接收 `SOAP Action`
2. 校验参数
3. 调用 `player` 或状态接口
4. 返回标准 SOAP 响应

### 4.4 播放层

职责：

1. 接收 `SetURI / Play / Pause / Stop / Seek`
2. 管理真实播放状态
3. 拉流并播放
4. 向控制层回报状态变化

### 4.5 事件层

职责：

1. 支持订阅
2. 在状态变化时发送通知

这层不是 MVP 必须项，但架构上要预留。

---

## 5. 当前阶段划分

### 5.1 已基本完成

1. 发现层基础能力
2. `device.xml`
3. `SCPD`
4. `SOAP` 路由与核心动作
5. `AVTransport / RenderingControl / ConnectionManager` 基础覆盖
6. 冒烟脚本

### 5.2 正在推进

1. `player` 从 mock 走向真实后端
2. 状态与控制层进一步收紧

### 5.3 后续能力

1. `GENA / LastChange`
2. `NOTIFY alive/byebye`
3. 更完整兼容性
4. 更强播放后端

---

## 6. 最小可用 DMR 链路

当前最小闭环应满足：

1. 控制端能看到 `NX-Cast`
2. 能访问 `device.xml`
3. 能访问三个 `SCPD`
4. 能发送 `SetAVTransportURI`
5. 能发送 `Play / Pause / Stop / Seek`
6. 能查询：
   1. `GetTransportInfo`
   2. `GetPositionInfo`
   3. `GetVolume / GetMute`

这条链路打通后，协议层就基本成立。

---

## 7. 服务职责细化

### 7.1 AVTransport

职责：

1. 当前媒体 URI
2. 播放状态
3. 播放位置
4. 播放控制

核心动作：

1. `SetAVTransportURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `Seek`
6. `GetTransportInfo`
7. `GetPositionInfo`

### 7.2 RenderingControl

职责：

1. 音量
2. 静音

核心动作：

1. `GetVolume`
2. `SetVolume`
3. `GetMute`
4. `SetMute`

### 7.3 ConnectionManager

职责：

1. 宣告能力
2. 提供协议与连接信息

核心动作：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

---

## 8. 状态设计

`DMR` 不是只返回成功与失败，还必须维护一套可查询状态。

最小状态集合：

1. `transport_uri`
2. `transport_uri_metadata`
3. `transport_state`
4. `transport_status`
5. `transport_speed`
6. `position`
7. `duration`
8. `volume`
9. `mute`

推荐状态来源：

1. `player`

控制层只是把这些状态映射到：

1. SOAP 响应
2. 未来事件通知

---

## 9. 事件设计

事件不是 MVP 必需，但架构上必须预留。

原因：

1. 手机控制和本地控制需要共享状态
2. 控制端 UI 有时依赖事件同步
3. `LastChange` 需要一个明确事件源

推荐事件源：

1. `player` 状态变化
2. 音量变化
3. 静音变化
4. URI 变化

推荐事件流：

1. `player` 发生变化
2. 控制层同步内部状态
3. 事件层决定是否对订阅者发送通知

---

## 10. 并发设计

推荐最小并发模型：

1. `SSDP` 线程
2. `HTTP/SOAP` 线程
3. `player backend` 线程

可选扩展：

1. 视频渲染线程
2. 音频输出线程

并发原则：

1. 发现层与控制层并行
2. 控制层与播放层并行
3. 但真实播放命令应在单一 backend 线程串行处理

这样做的原因：

1. 协议收包不阻塞播放
2. 播放状态不会被多线程同时修改
3. 更容易实现可预测状态机

---

## 11. 启动与停止顺序

推荐启动顺序：

1. `scpd_start`
2. `soap_server_start`
3. `ssdp_start`
4. `player_init`

如果 `player` 还不需要常驻初始化，也可在 `dlna_control` 中保持：

1. `player_init`
2. `scpd_start`
3. `soap_server_start`
4. `ssdp_start`

核心原则：

1. 对外可见前，描述和控制入口必须可用
2. 停止时按对外影响反向关闭

推荐停止顺序：

1. `ssdp_stop`
2. `soap_server_stop`
3. `scpd_stop`
4. `player_deinit`

---

## 12. 关于 SSDP 等待与 `MX`

发现阶段常见两个现象：

1. 测试脚本需要保留接收窗口
2. 设备端是否要按 `MX` 延迟响应

本项目当前建议：

1. 测试脚本保留短接收窗口
2. 设备端默认收到即回
3. `MX` 随机延迟响应作为 optional 能力保留

原因：

1. MVP 先保证发现稳定
2. 多设备冲突优化不是当前第一优先级

---

## 13. 当前实现顺序建议

### 阶段 1：协议层稳定

1. 保持 `SSDP -> device.xml -> SCPD -> SOAP` 全链路稳定
2. 保证脚本冒烟通过
3. 保证手机端可发现、可控制

### 阶段 2：真实播放接入

1. `SetAVTransportURI` 接到真实 `player`
2. `Play / Pause / Stop / Seek` 驱动真实后端
3. `GetPositionInfo` 读真实进度

### 阶段 3：事件与兼容性

1. 加入 `GENA / LastChange`
2. 增强不同控制端兼容性
3. 加入 optional 的 `NOTIFY`

---

## 14. 结论

`NX-Cast` 的 DMR 实现采用的是：

1. 分层架构
2. 控制面与数据面分离
3. URI 拉流式播放模型
4. 单一状态源
5. 增量实现路线

因此整个项目的核心不是“把所有协议一次性写满”，而是：

1. 先把发现、描述、控制链路稳定打通
2. 再把 `player` 接成真实后端
3. 最后补事件通知和兼容性增强

这条路线最适合当前 `NX-Cast` 的开发阶段。
