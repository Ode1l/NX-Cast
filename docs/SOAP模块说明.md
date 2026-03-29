# SOAP 模块说明（面向 NX-Cast）

本文档定义 `source/protocol/dlna/control/` 的职责边界、采用模式、请求处理链路和后续扩展方向。

---

## 1. 模块定位

`SOAP` 模块是 `DMR control plane` 的入口。

它负责：

1. 接收控制端发来的 `HTTP POST`
2. 解析 `SOAPACTION`
3. 解析 SOAP XML body
4. 将请求分发到具体 action 处理函数
5. 输出标准 SOAP 成功响应或 Fault

它不负责：

1. 设备发现
2. 设备描述与 SCPD 提供
3. 真实播放
4. 渲染与音频输出

一句话：

`SOAP` 模块只处理“控制命令”，不处理“发现”和“播放”。

---

## 2. 采用的模式

本模块采用以下模式。

### 2.1 控制入口模式

整个控制层只有一个统一入口：

1. `soap_server_start()`
2. `soap_server_stop()`

作用：

1. 对 `dlna_control` 暴露稳定生命周期
2. 把启动、停止、资源释放统一起来
3. 防止多个控制入口并存

### 2.2 路由分发模式

控制层采用“先解析，再路由”的结构，而不是在 HTTP 处理函数里直接写业务逻辑。

分工：

1. `soap_server.*`
   负责 HTTP 接入、SOAP 解析、响应输出
2. `soap_router.*`
   负责 `service + action -> handler` 的路由
3. `handler.*`
   负责公共参数、Fault、响应工具
4. `action/*.c`
   负责具体业务动作

这样做的目的：

1. HTTP 处理逻辑与业务逻辑分离
2. action 增长时结构仍可维护
3. 更容易做一致性校验和日志定位

### 2.3 表驱动路由模式

路由应由表驱动，而不是长链式 `if/else`。

路由键：

1. `service`
2. `action`

路由值：

1. `handler function pointer`

这样做的好处：

1. 新增 action 时改动范围小
2. 更容易和 SCPD 声明保持一致
3. 更适合打印统一日志

### 2.4 公共工具底座模式

所有 action 不应各自重复实现这些能力：

1. 取参
2. 必填参数校验
3. Fault 生成
4. 成功响应包装
5. 状态读写入口

因此这些能力应由 `handler.*` 统一提供。

### 2.5 Fault 工厂模式

SOAP Fault 的 XML 格式必须统一，因此错误输出不应分散在各 action 内手写。

建议统一由公共层输出：

1. `401 Invalid Action`
2. `402 Invalid Args`
3. `501 Action Failed`
4. 其他必要错误码

action 只需要返回：

1. 成功
2. 内部错误码
3. 描述字符串

### 2.6 状态单一来源模式

SOAP 层不是状态源。

状态来源应当是：

1. 当前控制状态容器
2. 更进一步是 `player`

SOAP action 的职责是：

1. 接收请求
2. 调用状态/播放接口
3. 把结果映射成 SOAP 返回

它不应自己发明状态。

---

## 3. 当前代码结构

目录：

```text
source/protocol/dlna/control/
  soap_server.h
  soap_server.c
  soap_router.h
  soap_router.c
  handler.h
  handler.c
  handler_internal.h
  action/
    avtransport.c
    renderingcontrol.c
    connectionmanager.c
```

各文件职责：

1. [soap_server.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/soap_server.c)
   作用：控制入口、HTTP/SOAP 解析、HTTP 响应输出
2. [soap_router.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/soap_router.c)
   作用：`service + action` 路由分发
3. [handler.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/handler.c)
   作用：公共取参、错误处理、响应工具
4. `action/*.c`
   作用：实现 `AVTransport / RenderingControl / ConnectionManager` 的具体动作

---

## 4. 请求处理链路

标准处理链路如下：

1. 控制端发起：
   `POST /upnp/control/{Service}`
2. `soap_server` 读取完整 HTTP 请求
3. 解析：
   1. `endpoint`
   2. `SOAPACTION`
   3. XML body
4. 提取：
   1. `service`
   2. `action`
5. 交给 `soap_router`
6. `soap_router` 找到目标 handler
7. action 通过 `handler` 公共工具取参和返回结果
8. `soap_server` 输出：
   1. `200 OK + SOAP Response`
   2. 或 `500 + SOAP Fault`

链路原则：

1. 解析在前
2. 路由在中
3. 业务在后
4. 输出统一由入口层完成

---

## 5. 参数处理策略

参数处理必须统一，不允许每个 action 各自定义风格。

建议规则：

1. 先按 action 声明读取参数
2. 对必填参数统一校验
3. 参数缺失直接返回 `402 Invalid Args`
4. 参数格式错误也返回 `402 Invalid Args`
5. 所有参数日志都带：
   1. `service`
   2. `action`
   3. `arg`

区分两类参数：

1. 必填参数
2. 可选参数

对外规则：

1. 缺必填参数必须失败
2. 缺可选参数可给默认值，但必须在 action 内明确写出来

---

## 6. 响应策略

### 6.1 成功响应

成功时：

1. `HTTP 200`
2. `Content-Type: text/xml; charset=\"utf-8\"`
3. 输出合法 SOAP Envelope

### 6.2 失败响应

失败时：

1. `HTTP 500`
2. 输出合法 SOAP Fault

最低覆盖错误：

1. 未知 service
2. 未知 action
3. 缺失 `SOAPACTION`
4. body 为空
5. 参数缺失
6. 参数非法
7. 内部执行错误

---

## 7. 状态与 Player 的关系

控制层有两类状态：

1. 协议状态
2. 播放状态

设计要求：

1. `SOAP` 负责把动作映射给 `player`
2. `player` 才是播放状态源
3. `SOAP` 只负责把 `player` 状态转成：
   1. `GetTransportInfo`
   2. `GetPositionInfo`
   3. `GetVolume`
   4. `GetMute`

因此下一阶段的正确方向是：

1. action 内逐步减少“只改内存状态”
2. action 内改为调用 `player_*`
3. 再把 `player` 事件同步回 SOAP 运行时状态

---

## 8. 并发与线程边界

`SOAP` 层不应自己承担长耗时工作。

原则：

1. HTTP 线程负责接收和返回
2. action 负责参数校验与命令下发
3. 真实播放线程由 `player/backend` 负责

不能做的事情：

1. 在 SOAP 线程里长时间拉流
2. 在 SOAP 线程里做重解码
3. 在 SOAP 线程里等待完整播放结束

这层的目标是“快进快出”，而不是“把播放器做在里面”。

---

## 9. 当前 MVP 范围

当前 SOAP 层的最小目标是覆盖三组服务的核心动作。

### 9.1 AVTransport

最低动作：

1. `SetAVTransportURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `GetTransportInfo`
6. `GetPositionInfo`
7. `Seek`

### 9.2 RenderingControl

最低动作：

1. `GetVolume`
2. `SetVolume`
3. `GetMute`
4. `SetMute`

### 9.3 ConnectionManager

最低动作：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

---

## 10. 日志策略

SOAP 层日志应统一分层，不和 `SCPD`、`HTTP`、`SSDP` 混用概念。

推荐日志维度：

1. `[soap]`
   记录 action 调用摘要
2. `[soap-router]`
   记录路由命中
3. `[soap-arg]`
   记录参数提取和校验
4. `[http-server]`
   记录 HTTP 收发

关键日志建议包含：

1. `service`
2. `action`
3. `handler`
4. `status code`
5. `fault code`

日志目标：

1. 出错时能快速定位在哪一层失败
2. 不需要展开完整 XML 也能看懂调用链

---

## 11. 生命周期设计

推荐启动顺序：

1. `scpd_start(...)`
2. `soap_server_start(...)`
3. `ssdp_start(...)`

推荐停止顺序：

1. `ssdp_stop()`
2. `soap_server_stop()`
3. `scpd_stop()`

原因：

1. 设备被发现前，描述和控制入口必须先准备好
2. 停止时先让外部无法继续发现，再关闭控制与描述

---

## 12. 下一阶段实现顺序

### 阶段 1

1. 稳定现有 SOAP 路由与 Fault 行为
2. 保证参数校验和日志一致
3. 保证空 body、截断 body、缺参数都稳定返回 Fault

### 阶段 2

1. 把高频 action 全部接到 `player_*`
2. 让 `Play/Pause/Stop/Seek/Volume/Mute` 驱动真实播放后端
3. 把 `player` 状态反向同步到控制层状态

### 阶段 3

1. 引入 `GENA / LastChange`
2. 建立事件通知链
3. 提高控制端 UI 同步能力

---

## 13. 结论

`SOAP` 模块在 `NX-Cast` 中采用的是：

1. 统一控制入口
2. 路由分发
3. 表驱动 action 映射
4. 公共工具底座
5. 统一 Fault 输出
6. 状态单一来源

因此它的角色非常明确：

1. 不是发现层
2. 不是描述层
3. 不是播放器
4. 它只是控制层入口和协议适配层

后续扩展都应沿着这个边界继续，而不是把播放逻辑重新塞回 SOAP 层。
