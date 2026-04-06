# 通用 DLNA DMR 源适配设计

本文档记录 `NX-Cast` 当前采用的通用源适配原则。

## 1. 目标

目标不是把 `NX-Cast` 做成站点原生客户端，而是：

1. 对标准 `DLNA DMR` 输入进行稳定建模
2. 尽可能从 metadata 中选择正确资源
3. 在不污染协议层的前提下提供最小必要策略

## 2. 非目标

本文档明确不把这些当作当前主线：

1. 在 `SOAP` 层直接写站点兼容逻辑
2. 用一个 `vendor` 直接推翻标准解析结果
3. 把“站点特化”当成第一层，而不是最后一层

## 3. 当前标准流水线

```text
standard input
  -> evidence
  -> IngressModel
  -> resource_select
  -> http_probe
  -> PlayerMedia
  -> policy
```

## 4. 当前 evidence

当前 evidence 主要来自：

1. URI
2. metadata
3. `protocolInfo`
4. sender 上下文
5. preflight 结果

它只回答事实，不直接写策略。

## 5. 当前 classify

classify 负责决定：

1. `format`
2. `transport`
3. `vendor`

当前重点是：

1. 标准证据优先
2. `vendor hint` 只能做加法

## 6. 当前 resource selection

当前 metadata 资源选择的目标是：

1. 优先使用标准 `res/protocolInfo`
2. 让 transport 分类建立在真实资源上
3. 避免“解析阶段就直接按站点硬写”

## 7. 当前 preflight

当前 `http_probe` 的职责是：

1. 辅助确认 `Content-Type`
2. 辅助确认 range/seekability
3. 辅助发现 redirect

它的作用是改进模型，而不是直接把 policy 写死。

## 8. 当前 policy

当前 policy 的职责是：

1. 细化 headers
2. 细化 timeout
3. 细化 load options
4. transport 级保护

它不能：

1. 重新解析 URI
2. 重新决定 `format/vendor/transport`

## 9. Macast 的启发

当前从 `Macast` 学到的主要不是某个站点 hack，而是：

1. 更完整的标准 DMR 行为
2. 更完整的 metadata 与状态面
3. 协议层和播放器层边界更清楚

这也是当前项目改造的方向：

1. 先把通用底座做完整
2. 再做少量 transport/vendor 特化

## 10. 当前阶段判断

当前已经不是“有没有 source adaptation”的问题，而是：

1. 这套适配架构是否干净
2. 是否先标准建模，再策略收尾
3. 是否避免再次退回规则堆叠
