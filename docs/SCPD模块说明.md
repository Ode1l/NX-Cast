# NX-Cast SCPD 模块说明

本文档说明当前 `source/protocol/dlna/description/scpd.c` 与 `scpd.h` 的实现，以及它和 `dlna_control` 的联动方式。

注：目录名使用 `scpd`，协议术语仍是 `SCPD`（Service Control Protocol Description）。

## 1. 模块职责

`scpd` 模块负责一个轻量 HTTP 服务，向控制端提供描述层资源：

1. `/device.xml`（设备描述）
2. `/scpd/AVTransport.xml`
3. `/scpd/RenderingControl.xml`
4. `/scpd/ConnectionManager.xml`

该模块仅负责描述层输出，不处理 SOAP 行为控制。

## 2. 对外接口

头文件：`source/protocol/dlna/description/scpd.h`

1. `bool scpd_start(uint16_t port, const ScpdConfig *config);`
2. `void scpd_stop(void);`

`ScpdConfig` 字段：

1. `friendly_name`
2. `manufacturer`
3. `model_name`
4. `uuid`

行为：

1. `scpd_start()` 在指定端口监听，并根据 `ScpdConfig` 动态生成 `device.xml`。
2. `scpd_stop()` 停止线程并关闭监听 socket。

## 3. 动态与静态边界

当前设计按“设备信息动态、服务定义静态”执行：

1. 动态生成：`device.xml` 中的 `friendlyName`、`manufacturer`、`modelName`、`UDN(uuid)`。
2. 静态内嵌：三个服务的 SCPD XML 内容（动作列表、状态变量、URL 路径）。
3. 静态路径：`/device.xml` 与 `/scpd/*.xml` 路径固定。

说明：

1. `uuid` 建议对同一逻辑设备保持稳定；变更后控制端通常会把它当作“新设备”。
2. `deviceType` 与三服务结构通常不需要频繁改动，适合静态内嵌。

## 4. 运行模型

实现为单线程 HTTP 循环：

1. 一个监听 socket。
2. 一个后台线程（`select` + `accept`）。
3. 每次连接读取一次请求并回包后关闭。
4. 仅处理 `GET`，其它方法返回 `404 Not Found`。

## 5. 与 DLNA Control 联动

`source/protocol/dlna_control.c` 已启用 SCPD，启动流程为：

1. 构建统一设备元数据（friendly/manufacturer/model/uuid）。
2. 先调用 `scpd_start(http_port, &scpdConfig)` 启动描述 HTTP。
3. 再调用 `ssdp_start(&ssdpConfig)` 对外发布发现信息。
4. 若 SSDP 启动失败，立即回滚执行 `scpd_stop()`。

停止流程：

1. `ssdp_stop()`
2. `scpd_stop()`

这样可保证 SSDP `LOCATION` 指向的 `device.xml` 已经可访问。

## 6. XML 文件放置说明

目录 `source/protocol/dlna/description/scpd/` 下保留了：

1. `AVTransport.xml`
2. `RenderingControl.xml`
3. `ConnectionManager.xml`

这些文件当前作为参考/备份；运行时由 `scpd.c` 内嵌字符串对外提供 XML。

## 7. 后续建议

1. SOAP 接入后，用同一份“服务描述表”同时驱动 SCPD 输出和 action 路由，避免定义漂移。
2. 需要更高可配置性时，再考虑把 SCPD 改为外部文件加载（当前阶段内嵌更稳、更易部署）。
