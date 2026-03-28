# DLNA 电视端（DMR）实现里程碑

这份文档面向“电视作为投屏接收端”的实现路线。

目标角色：**DMR（Digital Media Renderer）**

典型场景：
- 手机或电脑作为 **DMC（Digital Media Controller）**
- 手机、电脑、NAS 或其他媒体源作为 **DMS（Digital Media Server）**
- 电视作为 **DMR**，负责接收控制并播放内容

---

## 总体路线

标记说明：
- `[MVP]` 必须完成，属于最小可用链路
- `[可做]` 可选增强，提升兼容性/体验

建议把实现过程拆成 7 个里程碑：

1. 底座搭建
2. SSDP 发现层
3. `LOCATION -> device.xml`
4. 服务描述（SCPD）
5. 控制面（SOAP / AVTransport / RenderingControl / ConnectionManager）
6. 拉流与播放
7. 事件与联调

可以把整体链路概括为：

`SSDP发现 -> device.xml -> service description -> SOAP控制 -> 电视拉取媒体URL -> 本地播放`

---

## 里程碑 0：底座搭建

先把运行框架和基础资源准备好，不要一开始就直接写 SOAP。

### 目标
- [MVP] 建立一个可运行的单进程框架
- [MVP] 至少具备：
  - [MVP] 一个 UDP socket：负责 SSDP
  - [MVP] 一个 TCP HTTP server：负责 `device.xml`、SCPD、control URL
- [MVP] 生成并固定设备标识：
  - `uuid`
  - 设备名
  - 端口号

### 建议
- 第一版用 `select()` 即可
- 先做成单进程、少线程
- 先跑通网络，再考虑播放器和复杂状态同步

### 验收标准
- [MVP] UDP 1900 可以正常收发
- [MVP] HTTP 端口可以返回普通文本/XML

---

## 里程碑 1：SSDP 发现层

这是“让控制端找到你”的阶段。

### 要做的事

#### 1. 接收并解析 `M-SEARCH`
- [MVP] 监听 SSDP 多播地址和端口
- [MVP] 识别搜索报文
- [MVP] 解析关键字段：
  - `MAN: "ssdp:discover"`
  - `MX`
  - `ST`

#### 2. 响应 `M-SEARCH`
- [MVP] 用 UDP **单播**回给请求源地址和端口
- [MVP] 返回 `HTTP/1.1 200 OK`
- [MVP] 响应中至少包含：
  - `CACHE-CONTROL`
  - `LOCATION`
  - `ST`
  - `USN`
- [可做] 额外补上：`DATE` / `EXT` / `SERVER`

#### 2.1 关于“等待”和 `sleep`（Optional）
- [MVP] 测试脚本侧需要保留一个“接收窗口”（例如 `~1s`），否则 UDP 请求发出后程序可能立即退出，来不及收到回包。
- [MVP] 这类等待主要发生在测试脚本/控制端，不代表设备端必须延迟响应。
- [可做][Optional] 设备端可实现“按 `MX` 随机延迟再响应”（例如 `0 ~ MX` 秒内随机）。
- [可做][Optional] 该策略的效果：
  - 多设备同网段时，降低所有设备同时回包造成的瞬时冲突；
  - 代价是发现阶段平均时延会增加。
- [MVP] 当前阶段建议：设备端先“收到即回”，脚本侧保留小等待窗口，优先保证发现稳定性。

#### 3. 发送 `NOTIFY`
- [可做] 启动时发 `ssdp:alive`
- [可做] 运行中周期重发 `alive`
- [可做] 退出时发 `ssdp:byebye`
- [可做] 通知中至少包含：`HOST`/`CACHE-CONTROL`/`LOCATION`/`NT`/`NTS`/`USN`

### 最少要支持的 `ST`
- [MVP] `ssdp:all`
- [MVP] `upnp:rootdevice`
- [MVP] `uuid:device-UUID`
- [可做] 匹配的 device type / service type

### 关键理解
- `M-SEARCH` 是“别人来问，你回答”
- `NOTIFY` 是“你自己主动报到”
- 两种都实现，兼容性更好

### 验收标准
- [MVP] 控制端设备列表里能看到你
- [MVP] 抓包能看到 `M-SEARCH -> 200 OK`
- [可做] 抓包能看到 `NOTIFY alive`

---

## 里程碑 2：`LOCATION -> device.xml`

发现成功后，控制端会根据 SSDP 消息里的 `LOCATION` 去取设备描述 XML。

### 要做的事
- [MVP] 实现 `GET /device.xml`
- [MVP] 返回合法的设备描述 XML

### `device.xml` 中至少应包含
- [MVP] 设备基本信息
- [MVP] `UDN / uuid`
- [MVP] `serviceList`

### 建议声明的服务
对于电视端 DMR，建议至少挂这三类服务：
- `ConnectionManager`
- `AVTransport`
- `RenderingControl`

### 每个 `<service>` 至少给出
- `serviceType`
- `serviceId`
- `SCPDURL`
- `controlURL`
- `eventSubURL`

### 验收标准
- 浏览器能访问 `device.xml`
- 控制端能从中解析出服务列表和对应 URL

---

## 里程碑 3：服务描述（SCPD）

控制端读完 `device.xml` 后，通常还会继续请求每个服务对应的 SCPD XML。

### 要做的事
至少准备三个服务描述文件：
- `/service/AVTransport.xml`
- `/service/RenderingControl.xml`
- `/service/ConnectionManager.xml`

### SCPD 的作用
告诉控制端：
- 这个服务有哪些动作（actions）
- 每个动作有哪些参数（arguments）
- 这些参数关联哪些状态变量（state variables）

### 第一版建议
- 先保证 XML 合法
- 先保证 URL 能访问
- 不必一开始就把所有动作都实现完

### 验收标准
- 控制端能成功 GET 到所有 SCPD 文件
- XML 结构合法

---

## 里程碑 4：控制面（SOAP）

这一步才进入真正的“下命令”阶段。

控制端会对 `controlURL` 发 HTTP POST，body 中带 SOAP XML。

### 你需要做的基础能力
- 接收 HTTP POST
- 读取 `SOAPAction`
- 解析 XML body
- 分发到对应 service / action
- 返回合法 SOAP 响应

---

## 里程碑 4.1：先做最小可用的 AVTransport

这是电视端最重要的控制服务。

### 第一版建议先支持这些动作
- `SetAVTransportURI`
- `Play`
- `Pause`
- `Stop`

### 后续再补
- `GetMediaInfo`
- `GetTransportInfo`
- `GetPositionInfo`
- `Seek`
- `GetTransportSettings`
- `GetDeviceCapabilities`

### 语义理解
- `SetAVTransportURI`：保存当前媒体 URL
- `Play`：开始播放该 URL
- `Pause`：暂停
- `Stop`：停止并结束当前会话

### 验收标准
- 你可以用 curl / 测试工具发 SOAP
- 电视端能正确解析动作并返回响应

---

## 里程碑 4.2：实现最小 ConnectionManager

### 第一版建议先做
- `GetProtocolInfo`

### 为什么重要
控制端需要知道：
- 你能接收哪些协议
- 你支持哪些媒体格式

### 验收标准
- 控制端能成功调用 `GetProtocolInfo`
- 返回内容能表达基本的支持能力

---

## 里程碑 4.3：实现最小 RenderingControl

### 第一版建议先做
- 获取音量
- 设置音量
- 获取静音状态
- 设置静音状态

### 目的
给控制端提供最基本的渲染控制能力。

### 验收标准
- 调用对应 action 时能改变内部状态
- 能正确返回 SOAP 响应

---

## 里程碑 5：拉流与播放

这一步才是“真正开始投屏”。

### 核心理解
手机/电脑通常不是把视频数据直接推给电视，
而是通过 `SetAVTransportURI` 告诉电视一个媒体 URL，
然后电视**自己去拉这个 URL**。

### 建议实现顺序

#### 1. `SetAVTransportURI`
- 保存当前 URI
- 保存必要元数据

#### 2. `Play`
- 启动播放器线程或媒体线程
- 向该 URI 发起 HTTP GET
- 接收媒体数据
- 交给本地播放器或解码器

### 第一版建议
如果播放器还没接好，可以先做到：
- 电视端确实能对媒体 URL 发出 HTTP GET
- 确实能把数据读下来

### 状态机建议
至少维护这些状态：
- `NO_MEDIA_PRESENT`
- `STOPPED`
- `PLAYING`
- `PAUSED_PLAYBACK`
- `TRANSITIONING`

### 验收标准
- 调用 `SetAVTransportURI` 后状态更新正确
- 调用 `Play` 后电视端确实向目标 URL 发起请求
- 本地播放器开始播放，或至少能完成下载验证

---

## 里程碑 6：事件（Eventing）

这是更完整实现的一部分，建议放在第二阶段补。

### 为什么后做
因为如果前面的：
- SSDP
- `device.xml`
- SOAP 控制
- 拉流播放

都还没跑通，先做 Eventing 收益不高。

### 后续要做的事
- 提供 `eventSubURL`
- 支持订阅 / 退订
- 在状态变化时主动推送事件

### 典型事件来源
- 播放状态变化
- 音量变化
- 当前 URI 变化

### 验收标准
- 控制端能成功订阅事件
- 状态变化时能收到通知

---

## 里程碑 7：完整联调

最后把整条链路串起来验证。

### 完整链路

`NOTIFY alive` 或 `M-SEARCH`
-> `200 OK`
-> `GET /device.xml`
-> `GET /service/*.xml`
-> `SOAP SetAVTransportURI`
-> `SOAP Play`
-> 电视对媒体 URL 发 HTTP GET
-> 本地播放器开始播放

### 联调目标
- 控制端能发现电视
- 控制端能读取描述文件
- 控制端能调用控制接口
- 电视能主动拉流
- 电视能开始播放

---

## 推荐的实际编码顺序

建议按下面顺序推进：

1. UDP 1900 + SSDP parser
2. `M-SEARCH` 响应
3. `NOTIFY alive/byebye`
4. `device.xml`
5. 3 个 SCPD 文件
6. SOAP handler
7. `SetAVTransportURI` / `Play` / `Stop`
8. `GetProtocolInfo`
9. 媒体 HTTP client
10. 播放状态机
11. Eventing

---

## 最小可用 MVP

如果你的目标是“尽快完成一次成功投屏”，建议把 MVP 压缩成这 5 项：

1. SSDP：`M-SEARCH` + `NOTIFY`
2. Description：`device.xml`
3. Service description：3 个 SCPD
4. Control：`SetAVTransportURI` + `Play`
5. Media：电视真的去 GET 该 URL

做到这一步，你的电视端已经具备：
- 被发现
- 被识别
- 被控制
- 主动拉流
- 开始播放

---

## 你当前阶段的直接建议

如果你现在马上开始写 C 代码，建议先只盯住这三件事：

### 第一阶段
- `M-SEARCH` 响应做通
- `NOTIFY alive/byebye` 做通
- `LOCATION -> device.xml` 做通

### 第二阶段
- `AVTransport` 最小动作做通
- `ConnectionManager/GetProtocolInfo` 做通
- `Play` 后电视能主动 GET 媒体 URL

### 第三阶段
- 接上播放器
- 做事件订阅
- 做更完整的动作和状态查询

---

## 一句话总结

先把电视做成一个“能被发现、能暴露描述、能接受播放命令”的 **最小 DMR**，
再逐步补全媒体播放、状态机和事件系统。

你真正应该先做通的主链路是：

**发现你 -> 认识你 -> 命令你 -> 你自己去拉媒体并播放。**
