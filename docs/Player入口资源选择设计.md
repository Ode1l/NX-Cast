# Player 打开路径设计

这份文档保留为当前播放打开路径的摘要说明。

## 当前结论

1. `player` 当前不再保留独立的入口资源选择层
2. 协议层收到 `SetAVTransportURI` 后，直接调用 renderer
3. renderer 直接向 `libmpv` 发 `loadfile`
4. 媒体格式探测、拉流、demux 和 decode 交给 `libmpv/ffmpeg`

## 当前路径

当前打开路径是：

```text
CurrentURI / CurrentURIMetaData
  -> renderer_set_uri(...)
  -> libmpv loadfile
  -> libmpv observed properties / events
  -> PlayerSnapshot / PlayerEvent
  -> protocol_state
```

## 当前原则

当前路径刻意保持很薄：

1. 不在 `player` 内再做一套独立推断流水线
2. 不手工 hint 媒体格式
3. 不让协议层知道 backend 内部细节
4. 把更多精力放在运行时状态同步和协议互操作上

## 当前应阅读

1. [Player层设计.md](Player层设计.md)
2. [DMR实现细节.md](DMR实现细节.md)
3. [SCPD模块说明.md](SCPD模块说明.md)
