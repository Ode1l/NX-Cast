# Player 入口资源选择设计

这份文档当前作为入口资源选择的摘要说明保留。

## 当前结论

1. 入口资源选择已经从“占位概念”变成当前 `ingress` 的正式一环
2. 当前资源选择发生在 `IngressModel` 上，而不是直接修改最终 `PlayerMedia`
3. 资源选择的目标是让 `format / transport / vendor` 建立在更真实的资源之上

## 当前位置

资源选择当前位于：

- `source/player/ingress/resource_select.c`

它处于这条流水线中：

```text
evidence
  -> IngressModel
  -> resource_select
  -> http_probe
  -> PlayerMedia
  -> policy
```

## 当前应阅读

1. [Player层设计.md](Player层设计.md)
2. [通用DLNA-DMR源适配设计.md](通用DLNA-DMR源适配设计.md)
3. [源兼容性.md](源兼容性.md)
