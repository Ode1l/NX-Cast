# SOAP 模块说明

本文档描述 `source/protocol/dlna/control/` 当前的控制层实现。由于 `GENA eventing` 已经并入同一目录，本文档也同时覆盖事件控制边界。

---

## 1. 模块范围

当前目录：

```text
source/protocol/dlna/control/
  soap_server.*
  soap_router.*
  soap_writer.*
  handler.*
  event_server.*
  action/
    avtransport.c
    renderingcontrol.c
    connectionmanager.c
```

职责分工：

1. `soap_server.*`
   - HTTP/SOAP 入口、Envelope/Fault 包装
2. `soap_router.*`
   - `service + action` 路由
3. `soap_writer.*`
   - 动态 XML 输出与统一 escape
4. `handler.*`
   - 公共取参、XML 实体解码、运行时状态、公共工具
5. `event_server.*`
   - `SUBSCRIBE / UNSUBSCRIBE / NOTIFY / LastChange`
6. `action/*.c`
   - 三个标准服务的具体动作

---

## 2. 当前请求链路

### 2.1 SOAP

当前 `SOAP` 请求链路：

```text
HTTP POST
  -> soap_server
  -> soap_router
  -> action handler
  -> player / runtime state
  -> soap_writer
  -> HTTP 200 or SOAP Fault
```

### 2.2 Eventing

当前 `GENA` 事件链路：

```text
HTTP SUBSCRIBE / UNSUBSCRIBE
  -> event_server
  -> subscription table

player event
  -> handler runtime state
  -> event_server worker
  -> NOTIFY + LastChange
```

---

## 3. 当前已解决的问题

相较于早期版本，控制层已经不再存在这些缺口：

1. `CurrentURIMetaData` 和 `TrackMetaData` 不再是空壳
2. `GetCurrentTransportActions` 已实现
3. `SOAP` 输出不再依赖大块固定栈数组
4. XML escape / entity decode 已统一
5. `SUBSCRIBE / NOTIFY / LastChange` 已接通

其中最关键的结构升级是：

1. [soap_writer.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/soap_writer.c)
2. [soap_server.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/soap_server.c)

这让控制层从“手拼 XML + 固定缓冲”升级成“动态 writer + 单一输出模型”。

---

## 4. 当前动作覆盖

### 4.1 AVTransport

主要动作：

1. `SetAVTransportURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `Seek`
6. `GetTransportInfo`
7. `GetMediaInfo`
8. `GetPositionInfo`
9. `GetCurrentTransportActions`

当前实现特点：

1. `Play/Pause/Seek/Stop` 都以 `player snapshot` 为真实状态依据
2. 过渡态中的控制动作已做兼容性收敛，不再简单以“瞬时状态”粗暴拒绝
3. `SetAVTransportURI` 支持传递 `sender` 请求上下文到 player ingress

### 4.2 RenderingControl

已实现：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`

当前未实现：

1. `GetBrightness` 等图像调节动作

说明：

1. 某些控制端会探测这些动作
2. 当前返回 `401 Invalid Action` 属于可接受的标准行为

### 4.3 ConnectionManager

已实现：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

当前意义：

1. `SinkProtocolInfo` 已不再是窄范围占位字符串
2. 其内容会直接影响控制端对资源类型和设备能力的判断

---

## 5. 当前状态来源

控制层不再自创播放状态。

当前真实状态链路是：

1. `player` 产出 `PlayerSnapshot` 与 `PlayerEvent`
2. [handler.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/protocol/dlna/control/handler.c) 把它们映射成 `SOAP runtime state`
3. `SOAP` 读查询接口
4. `GENA` 读事件接口

因此：

1. `SOAP` 不是状态源
2. `GENA` 也不是状态源
3. 二者只是同一份真实状态的协议投影

---

## 6. 当前并发边界

控制层的并发模型是：

1. `HTTP` 线程接请求
2. `player owner thread` 串行执行重操作
3. `event worker` 发送 `NOTIFY`

控制层不做的事：

1. 不直接执行重型播放命令
2. 不持有平台 render 资源
3. 不自己维护第二套播放器状态机

---

## 7. 当前与成熟 DMR 的差距

控制层现在已经不是主短板，但仍有三个差距：

1. `LastChange` 内容还可以继续补全
2. 控制端兼容矩阵还需要继续积累
3. 一些动作返回仍有兼容性降噪空间

真正更大的差距已经转移到：

1. 通用媒体能力
2. 完整播放器后端
3. 复杂源 transport 处理

---

## 8. 当前结论

一句话总结：

`source/protocol/dlna/control` 当前已经从“基础 SOAP handler”升级成“SOAP + GENA + 动态 writer + 真实状态投影”的正式控制层，后续这里只需要继续做协议兼容细化，不再是系统主战场。
