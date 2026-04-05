# Player 入口资源选择设计

这份占位文档已并入主文档，不再单独维护。

当前应阅读：

1. [Player层设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/Player层设计.md)
2. [源兼容性.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/源兼容性.md)

当前状态：

1. `CurrentURIMetaData` 的 `res/protocolInfo` 候选资源选择已落到 `player/ingress`
2. 当前实现已经是 `evidence -> classify -> resource_select -> policy` 的第一版
3. 后续工作重点不再是“有没有入口资源选择”，而是评分和 transport 规则继续完善
