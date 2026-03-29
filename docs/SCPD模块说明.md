# SCPD 模块说明（面向 NX-Cast）

本文档说明 `source/protocol/dlna/description/scpd.c` 与 `scpd.h` 的职责、数据组织方式和与其他模块的联动关系。

注：

1. 目录名使用 `scpd`
2. 协议术语仍是 `SCPD`，即 `Service Control Protocol Description`

---

## 1. 模块定位

`SCPD` 模块属于描述层。

它负责：

1. 提供 `device.xml`
2. 提供三个服务的 `SCPD XML`
3. 对外声明设备能力、服务列表、控制路径和事件路径

它不负责：

1. 监听 socket
2. 管理 HTTP 线程
3. 执行 SOAP action
4. 维护真实播放状态

一句话：

`SCPD` 负责“告诉控制端我是什么、我支持什么”，不负责“真正执行控制命令”。

---

## 2. 采用的模式

### 2.1 描述资源提供者模式

`SCPD` 作为一个纯资源提供模块存在，由外部 HTTP 层在请求到达时调用它生成响应。

作用：

1. 描述层不自己开线程
2. 描述层不感知 socket 细节
3. 描述层只关心：
   1. 资源内容
   2. 路径匹配
   3. HTTP 响应内容生成

### 2.2 静态服务定义 + 动态设备信息模式

当前模块采用“服务定义静态、设备信息动态”的混合模式。

静态部分：

1. `AVTransport` 的 `SCPD XML`
2. `RenderingControl` 的 `SCPD XML`
3. `ConnectionManager` 的 `SCPD XML`
4. 各服务的路径和 URL 结构

动态部分：

1. `friendlyName`
2. `manufacturer`
3. `modelName`
4. `uuid / UDN`

这样做的原因：

1. 服务结构在 MVP 阶段相对稳定
2. 设备标识需要按运行配置注入
3. 部署简单，不依赖文件系统加载 XML

### 2.3 资源表模式

模块内部把描述资源看作一张资源表，而不是分散的条件判断。

资源表元素至少包含：

1. `path`
2. `body`
3. `body_len`

这样做的好处：

1. 路径与资源一一对应
2. 新增资源时改动范围小
3. 更适合统一处理 `GET /device.xml` 和 `/scpd/*.xml`

### 2.4 统一 HTTP handler 模式

`SCPD` 不直接暴露多个 URL 处理函数，而是只暴露一个统一入口：

1. `scpd_try_handle_http(...)`

作用：

1. 外部 HTTP 层只需问一次“这个请求是不是描述层请求”
2. 描述层自己判断：
   1. 方法
   2. 路径
   3. 是否命中资源

### 2.5 固定 URL 拓扑模式

当前描述层对外 URL 结构固定：

1. `/device.xml`
2. `/scpd/AVTransport.xml`
3. `/scpd/RenderingControl.xml`
4. `/scpd/ConnectionManager.xml`

优点：

1. 控制端稳定
2. SSDP `LOCATION` 稳定
3. `device.xml` 与 `SCPDURL/controlURL/eventSubURL` 更容易保持一致

---

## 3. 当前代码结构

文件：

1. [scpd.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/description/scpd.h)
2. [scpd.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/description/scpd.c)

对外接口：

1. `bool scpd_start(const ScpdConfig *config);`
2. `void scpd_stop(void);`
3. `bool scpd_try_handle_http(const char *method, const char *path, char *response, size_t response_size, size_t *response_len);`

配置结构：

1. `friendly_name`
2. `manufacturer`
3. `model_name`
4. `uuid`

当前实现要点：

1. `device.xml` 通过模板 + 运行时参数生成
2. 三个服务 XML 为内嵌字符串常量
3. 使用资源表统一管理对外路径

---

## 4. 资源组织方式

### 4.1 `device.xml`

`device.xml` 采用模板生成模式。

当前模板中固定的内容：

1. `deviceType`
2. `serviceList`
3. 三个服务的：
   1. `serviceType`
   2. `serviceId`
   3. `SCPDURL`
   4. `controlURL`
   5. `eventSubURL`

当前运行时注入的内容：

1. `friendlyName`
2. `manufacturer`
3. `modelName`
4. `UDN`

### 4.2 三个服务 XML

当前三个服务 XML 为内嵌静态字符串：

1. `AVTransport`
2. `RenderingControl`
3. `ConnectionManager`

其中包含：

1. `actionList`
2. `argumentList`
3. `serviceStateTable`

### 4.3 资源表

当前资源表对应：

1. `/device.xml`
2. `/scpd/AVTransport.xml`
3. `/scpd/RenderingControl.xml`
4. `/scpd/ConnectionManager.xml`

这意味着描述层对外资源集合是固定且可枚举的。

---

## 5. 动态与静态边界

当前建议保持如下边界。

### 5.1 应动态生成的部分

1. `friendlyName`
2. `manufacturer`
3. `modelName`
4. `uuid / UDN`

原因：

1. 这些字段属于设备实例信息
2. 同一份程序在不同构建或不同设备身份下可能不同

### 5.2 应静态保留的部分

1. `deviceType`
2. 三个服务的声明
3. `controlURL`
4. `eventSubURL`
5. 各服务 `actionList`
6. 各服务 `serviceStateTable`

原因：

1. 这些属于协议结构定义
2. 在当前阶段不需要运行时变化
3. 写死更稳，也更容易调试

### 5.3 `uuid` 是否应变化

建议：

1. 对同一逻辑设备保持稳定
2. 不要每次启动随机变化

原因：

1. 控制端通常把 `uuid` 当成设备身份
2. 频繁变化会被视为“新设备”

---

## 6. 与 HTTP 层的关系

`SCPD` 当前采用“公共 HTTP 层 + 描述 handler”的结构。

职责边界：

1. `http_server`
   负责监听、accept、recv、send、线程管理
2. `scpd`
   负责：
   1. 方法判断
   2. 路径判断
   3. 构造描述响应

这样做的优点：

1. `SOAP` 和 `SCPD` 可以共用一个 HTTP 服务
2. 避免重复起多个 HTTP 线程
3. `SCPD` 更像“资源模块”，不是“服务器模块”

---

## 7. 与 SOAP 层的关系

`SCPD` 与 `SOAP` 的关系是“声明能力”和“执行能力”的关系。

`SCPD` 负责声明：

1. 有哪些服务
2. 每个服务有哪些 action
3. 每个 action 有哪些参数

`SOAP` 负责执行：

1. 接收 action 调用
2. 校验参数
3. 调用具体实现
4. 返回结果

设计要求：

1. `SCPD` 中声明的 action 应与 `SOAP` 路由保持一致
2. 不应出现：
   1. `SCPD` 声明了，但 `SOAP` 没实现
   2. `SOAP` 实现了，但 `SCPD` 没声明

后续建议：

1. 逐步收敛成“同一份服务元数据驱动 SCPD 和 SOAP”

---

## 8. 生命周期设计

推荐初始化顺序：

1. 构造 `ScpdConfig`
2. 调用 `scpd_start(&config)`
3. 再启动 `soap_server`
4. 再启动公共 `http_server`
5. 最后启动 `ssdp`

推荐停止顺序：

1. `ssdp_stop()`
2. `http_server_stop()`
3. `soap_server_stop()`
4. `scpd_stop()`

原则：

1. 对外可发现前，描述资源必须已经准备好
2. 对外停止时，先断发现，再断控制和描述

---

## 9. 外部 XML 文件的定位

项目中的 `xml/` 目录当前应视为参考模板或备份资源，而不是运行时资源来源。

当前运行方式：

1. `GET /device.xml`
   来自运行时模板生成
2. `GET /scpd/*.xml`
   来自内嵌字符串

这意味着：

1. 程序运行不依赖外部 XML 文件存在
2. 文件系统里缺少这些 XML，不会影响当前描述服务

---

## 10. 当前设计的优点与边界

当前设计的优点：

1. 部署简单
2. 调试直接
3. 不依赖文件系统读取
4. 与公共 HTTP 层耦合清晰

当前边界：

1. 服务定义仍是手工维护
2. `SCPD` 和 `SOAP` 一致性需要人工保证
3. 高可配置性暂时不足

这些边界在当前阶段是可接受的，因为当前目标是“描述层稳定可用”，不是“动态生成所有协议定义”。

---

## 11. 后续演进方向

### 阶段 1

1. 保持现有内嵌 XML 结构
2. 保证与现有 SOAP action 覆盖一致
3. 保持 `device.xml` 动态字段稳定

### 阶段 2

1. 建立统一服务元数据
2. 用同一份数据同时驱动：
   1. `SCPD` 输出
   2. `SOAP` 路由

### 阶段 3

1. 若确实需要高可配置性，再评估外部 XML 加载
2. 但外部文件加载不应早于统一元数据收敛

---

## 12. 结论

`SCPD` 模块在 `NX-Cast` 中采用的是：

1. 描述资源提供者模式
2. 静态服务定义 + 动态设备信息模式
3. 资源表模式
4. 统一 HTTP handler 模式
5. 固定 URL 拓扑模式

因此它的边界非常明确：

1. 它不是 HTTP 服务器
2. 它不是 SOAP 控制器
3. 它不是播放器
4. 它只是描述层资源模块

后续扩展应继续沿着这个边界推进，而不是把控制逻辑塞进 `SCPD`。
