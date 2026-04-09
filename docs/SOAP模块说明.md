# SOAP 模块说明

本文档描述 `source/protocol/dlna/control/` 当前控制层的真实职责与边界。

## 1. 当前目录

```text
source/protocol/dlna/control/
  action/
  event_server.*
  handler.*
  protocol_state.*
  soap_router.*
  soap_server.*
  soap_writer.*
```

## 2. 当前分工

### 2.1 soap_server

负责：

1. HTTP 入口
2. SOAP Envelope / Fault 包装
3. 把请求交给 router

### 2.2 soap_router

负责：

1. `service + action` 路由

### 2.3 soap_writer

负责：

1. 动态 XML 输出
2. 统一 XML escape

### 2.4 handler

负责：

1. 取参
2. XML entity 解码
3. 与 `player` 的桥接
4. 把 player 事件同步进协议状态

### 2.5 protocol_state

负责：

1. 维护协议观察状态单一真源
2. 供 `SOAP` 查询读取
3. 供 `LastChange` 读取
4. 供兼容辅助逻辑读取

这是当前最重要的结构变化之一。

### 2.6 event_server

负责：

1. `SUBSCRIBE / UNSUBSCRIBE`
2. `NOTIFY`
3. `LastChange`

## 3. 当前控制链

### 3.1 SOAP

```text
HTTP POST
  -> soap_server
  -> soap_router
  -> action
  -> player / protocol_state
  -> soap_writer
  -> HTTP response
```

### 3.2 Eventing

```text
SUBSCRIBE / UNSUBSCRIBE
  -> event_server

player event
  -> handler
  -> protocol_state
  -> event_server worker
  -> NOTIFY / LastChange
```

## 4. 当前原则

控制层当前按这些原则实现：

1. 协议层只做协议工作
2. 协议层不直接做站点适配
3. 协议状态只有一份
4. `SOAP` 查询和事件推送共享同一份协议状态
5. `player` 仍是唯一真实播放状态源

## 5. 当前动作范围

### 5.1 AVTransport

当前实现：

1. `SetAVTransportURI`
2. `Play / Pause / Stop / Seek`
3. `GetTransportInfo`
4. `GetMediaInfo`
5. `GetPositionInfo`
6. `GetCurrentTransportActions`

### 5.2 RenderingControl

当前实现：

1. `GetVolume / SetVolume`
2. `GetMute / SetMute`
3. `GetBrightness` 兼容 stub

### 5.3 ConnectionManager

当前实现：

1. `GetProtocolInfo`
2. `GetCurrentConnectionIDs`
3. `GetCurrentConnectionInfo`

## 6. 当前不再采用的做法

当前已经避免这些旧方式：

1. 每个 action 自己维护一份运行时状态
2. `LastChange` 再维护一份独立状态
3. 固定大缓冲手拼 XML
4. 把 `SOAP` 层写成站点兼容层

## 7. 当前重点

控制层当前重点不是继续加 action 数量，而是：

1. 保持协议观察状态一致
2. 提高控制端兼容
3. 让查询、事件、动作语义继续统一

## 8. 相关文档

1. [DMR实现细节.md](DMR实现细节.md)
2. [SCPD模块说明.md](SCPD模块说明.md)
3. [源兼容性.md](源兼容性.md)
