# NX-Cast SOAP 模块说明（MVP）

本文档定义 `source/protocol/dlna/control/` 的 SOAP 控制层设计与落地顺序。

## 1. 模块定位

SOAP 模块负责“控制层”请求处理，不负责发现层和描述层：

1. 发现层：`SSDP`
2. 描述层：`device.xml + SCPD`
3. 控制层：`SOAP Action 路由 + Action Handler`

目标是把控制端发来的 `HTTP POST + SOAP` 请求分发到对应动作处理函数，并返回标准 SOAP 响应。

## 2. 目录与文件分工

目录：`source/protocol/dlna/control/`

1. `soap_server.h / soap_server.c`
- 控制层入口（`start/stop`）
- HTTP `POST /upnp/control/*` 接入
- 读取 `SOAPACTION`、提取请求体、组织响应

2. `soap_router.h / soap_router.c`
- 只做“路由”
- 把 `service + action` 映射到 handler
- 不做业务逻辑

3. `action/handler.h / action/handler.c`
- 具体动作处理（状态读写、参数校验）
- 返回动作结果或错误码

## 3. 请求处理链路

1. 控制端 `POST /upnp/control/{Service}`，携带 `SOAPACTION` 与 XML Body。
2. `soap_server.c` 解析 HTTP 头，提取：
- 控制 URL 中的 service（如 `AVTransport`）
- `SOAPACTION` 中的 action（如 `Play`）
- SOAP body 参数
3. `soap_router.c` 按 `service + action` 分发到 `handler.c`。
4. `handler.c` 执行动作，返回业务结果。
5. `soap_server.c` 组装：
- 成功响应（`200 OK + SOAP Envelope`）
- 或错误响应（SOAP Fault）

## 4. 路由设计（建议表驱动）

建议在 `soap_router.c` 用表定义路由，避免大量 `if/else`：

1. 路由键：`service_id + action_name`
2. 路由值：`handler function pointer`
3. 未命中：返回 `Invalid Action` Fault

这能和后续 SCPD 描述保持一致，降低“声明了 action 但没有实现”的风险。

## 5. MVP 实现顺序

第一阶段（先打通）：

1. `SetAVTransportURI`
2. `Play`

第二阶段（补齐基础控制）：

1. `Pause`
2. `Stop`
3. `GetTransportInfo`

第三阶段（扩展服务）：

1. RenderingControl：`GetVolume/SetVolume`
2. ConnectionManager：`GetProtocolInfo/GetCurrentConnectionIDs`

## 6. 与 `dlna_control` 的启动联动

建议启动顺序：

1. `scpd_start(...)`
2. `soap_server_start(...)`
3. `ssdp_start(...)`

失败回滚顺序（反向）：

1. 若 `soap_server_start` 失败：`scpd_stop`
2. 若 `ssdp_start` 失败：`soap_server_stop -> scpd_stop`

停止顺序建议：

1. `ssdp_stop()`
2. `soap_server_stop()`
3. `scpd_stop()`

这样可确保设备被发现前，描述和控制入口都已准备好。

## 7. 响应与错误约定（MVP）

成功：

1. `HTTP 200`
2. `Content-Type: text/xml; charset="utf-8"`
3. 合法 SOAP Envelope

错误（至少覆盖）：

1. 未知 service/action
2. 缺少 `SOAPACTION`
3. 参数缺失或类型错误
4. 非法请求方法（非 POST）

建议统一由 `soap_server.c` 输出 Fault 模板，handler 只返回内部错误码。

## 8. 验收标准

1. 控制端可成功调用 `SetAVTransportURI + Play` 并收到合法 SOAP 响应。
2. 未实现 action 能返回可识别 Fault，而不是连接断开或空响应。
3. `SCPD` 中已声明的 action 与路由表能一一对应（至少在 MVP 范围内）。

## 9. gmrender 可借鉴设计（分期落地）

| 设计点 | 适合阶段 | 为什么 | 建议落点 |
|---|---|---|---|
| 表驱动 Action 路由（`service + action -> handler`） | 现在实现 | 直接降低 `if/else` 分支复杂度，并且最容易保证和 SCPD 一致 | `source/protocol/dlna/control/soap_router.c` |
| SOAP 通用工具函数（取参/回包/Fault） | 现在实现 | 先把错误处理和响应格式统一，后续加 action 不会重复写样板代码 | `source/protocol/dlna/control/soap_server.c` |
| 统一控制入口（`soap_server_start/soap_server_stop`） | 现在实现 | 能快速接入 `dlna_control` 生命周期，形成完整启动/停止链路 | `source/protocol/dlna/control/soap_server.h` + `soap_server.c` |
| 服务实现与框架解耦（router/handler 分层） | 现在实现 | 你已经建好 `action/` 目录，继续按职责拆分最合适 | `action/router.*` + `action/handler.*` |
| 状态容器（集中管理变量） | 后续补充 | MVP 先打通控制链路更重要，状态容器可在 action 变多后再引入 | 新建 `source/protocol/dlna/control/state.*`（建议） |
| LastChange 事件聚合与事务提交 | 后续补充 | 需要配合事件订阅（GENA）与变量体系，复杂度较高，不应阻塞 SOAP MVP | 依赖未来事件模块 |
| 从元数据自动生成 SCPD + 路由一致性校验 | 后续补充 | 这是长期收益项，先手写稳定后再做自动化更稳妥 | 描述层与控制层共享元数据模块 |

## 10. 推荐实施顺序（基于当前代码）

1. 在 `soap_server.c` 完成 HTTP POST 接入、`SOAPACTION` 解析、SOAP/Fault 模板输出。
2. 在 `soap_router.c` 建立表驱动路由（先覆盖 `AVTransport:SetAVTransportURI/Play`）。
3. 在 `handler.c` 实现两个最小 action，并返回标准成功响应。
4. 在 `dlna_control.c` 接入 `soap_server_start/soap_server_stop`，形成 `SCPD -> SOAP -> SSDP` 启动链。
5. 通过控制端做冒烟验证，再扩展 `Pause/Stop/GetTransportInfo`。

## 11. 三服务 MVP 设计表（可直接实现）

### 11.1 AVTransport

| Action | 入参（最小） | 出参（最小） | 状态变化 | 优先级 |
|---|---|---|---|---|
| `SetAVTransportURI` | `InstanceID`, `CurrentURI`, `CurrentURIMetaData` | 无 | 保存 `CurrentURI/MetaData`，`TransportState=STOPPED` | P0 |
| `Play` | `InstanceID`, `Speed` | 无 | 若有 URI，`TransportState=PLAYING`，否则返回错误 | P0 |
| `Pause` | `InstanceID` | 无 | `TransportState=PAUSED_PLAYBACK` | P1 |
| `Stop` | `InstanceID` | 无 | `TransportState=STOPPED` | P1 |
| `GetTransportInfo` | `InstanceID` | `CurrentTransportState`, `CurrentTransportStatus`, `CurrentSpeed` | 无 | P1 |

### 11.2 RenderingControl

| Action | 入参（最小） | 出参（最小） | 状态变化 | 优先级 |
|---|---|---|---|---|
| `GetVolume` | `InstanceID`, `Channel` | `CurrentVolume` | 无 | P2 |
| `SetVolume` | `InstanceID`, `Channel`, `DesiredVolume` | 无 | 更新 `Volume`（建议限制 `0~100`） | P2 |

### 11.3 ConnectionManager

| Action | 入参（最小） | 出参（最小） | 状态变化 | 优先级 |
|---|---|---|---|---|
| `GetProtocolInfo` | 无 | `Source`, `Sink` | 无 | P2 |
| `GetCurrentConnectionIDs` | 无 | `ConnectionIDs` | 无 | P2 |

## 12. 状态与错误约定（MVP）

### 12.1 建议最小状态集合

1. `transport_uri`（字符串）
2. `transport_uri_metadata`（字符串）
3. `transport_state`（`STOPPED/PLAYING/PAUSED_PLAYBACK/NO_MEDIA_PRESENT`）
4. `transport_status`（默认 `OK`）
5. `transport_speed`（默认 `"1"`）
6. `volume`（默认 `20`）
7. `connection_ids`（默认 `"0"`）
8. `source_protocol_info/sink_protocol_info`（先用固定字符串）

### 12.2 建议 Fault 映射

1. 未知服务或未知 action：`401 Invalid Action`
2. 参数缺失/参数非法：`402 Invalid Args`
3. 无媒体时执行 `Play`：`701 Transition not available`（或先用通用 Action Failed）
4. 内部错误：`501 Action Failed`
