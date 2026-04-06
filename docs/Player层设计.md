# Player 层设计

本文档记录 `NX-Cast` 当前 `player` 层的真实结构，不再混入早期未落地的假设。

## 1. 目标

`player` 层解决四件事：

1. 接收统一播放命令
2. 维护真实播放状态
3. 协调具体播放后端
4. 向协议层提供稳定的 snapshot 和 event

它的目标不是：

1. 直接承载协议逻辑
2. 直接承载站点兼容规则
3. 直接承载平台 UI 生命周期

## 2. 当前边界

当前分层：

```text
player
  -> core
  -> ingress
  -> backend
  -> render
```

### 2.1 core

当前职责：

1. owner thread
2. 命令队列
3. snapshot
4. event callback
5. backend 串行 ownership

核心文件：

- `source/player/core/session.c`

### 2.2 ingress

当前职责：

1. 标准输入建模
2. metadata 资源选择
3. URL preflight
4. transport 分类
5. 最终 media materialize
6. policy 应用

核心文件：

- `source/player/ingress/model.h`
- `source/player/ingress/ingress.c`
- `source/player/ingress/classify.c`
- `source/player/ingress/resource_select.c`
- `source/player/ingress/http_probe.c`

### 2.3 backend

当前职责：

1. 真实播放
2. 状态同步
3. 音量/静音/seek/stop
4. 与 `libmpv` 对接

当前默认真实 backend：

- `libmpv`

### 2.4 render

当前职责：

1. 前台视频页切换
2. render context 生命周期
3. 与 backend 的窄 render 接口对接

## 3. 当前公共接口

`player` 对外只暴露三类东西：

1. 命令
2. `PlayerSnapshot`
3. `PlayerEvent`

### 3.1 命令

包括：

1. `open/set uri`
2. `play`
3. `pause`
4. `stop`
5. `seek`
6. `volume`
7. `mute`

### 3.2 PlayerSnapshot

`PlayerSnapshot` 表示“播放器当前是什么状态”，而不是“刚发生了什么”。

当前包含：

1. `has_media`
2. `media`
3. `state`
4. `position_ms`
5. `duration_ms`
6. `volume`
7. `mute`
8. `seekable`

### 3.3 PlayerEvent

`PlayerEvent` 表示“刚发生了什么变化”。

当前只保留真正通用的运行时信息：

1. `type`
2. `state`
3. `position_ms`
4. `duration_ms`
5. `volume`
6. `mute`
7. `seekable`
8. `error_code`
9. `uri`

不再把 `ingress` 内部概念通过事件面泄露出去。

## 4. 当前 ingress 原则

当前 `ingress` 已明确采用：

```text
evidence
  -> IngressModel
  -> resource_select
  -> http_probe
  -> PlayerMedia
  -> policy
```

这里要严格区分：

1. 解析：这是什么源
2. 策略：怎么更稳地打开它

### 4.1 IngressModel

`IngressModel` 是标准输入的统一模型。

它先承载：

1. `CurrentURI`
2. `CurrentURIMetaData`
3. sender 请求上下文
4. metadata 资源选择结果
5. probe 结果
6. `format/vendor/transport`

然后再 materialize 成最终 `PlayerMedia`。

### 4.2 transport

当前显式 transport 枚举：

1. `http-file`
2. `hls-direct`
3. `hls-local-proxy`
4. `hls-gateway`

### 4.3 flags

当前 `PlayerMediaFlags` 只保留非冗余提示：

1. `likely_live`
2. `is_signed`
3. `likely_segmented`
4. `likely_video_only`

凡是能由 `format/vendor/transport` 推导出的信息，不再重复存成布尔。

## 5. 当前 policy 原则

`policy` 是 ingress 的最后一层。

它只能：

1. 细化 `headers`
2. 设置 `timeout`
3. 设置 `load options`
4. 细化 transport/vendor 的打开策略

它不能：

1. 重新解析 URI
2. 重新决定 `format`
3. 重新决定 `transport`
4. 重新决定 `vendor`

## 6. 当前 render/backend 路线

当前已落地路线：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

当前未真正落地：

1. explicit `nvtegra` hwdec backend
2. `deko3d` render path

## 7. 后续方向

当前 `player` 层的后续方向是：

1. 继续薄化 `PlayerSnapshot.media`
2. 继续收口 transport 与 seek policy
3. 继续稳定 mixed/local-proxy transport
4. 后续再评估自定义媒体工具链

## 8. 相关文档

1. [DMR实现细节.md](DMR实现细节.md)
2. [源兼容性.md](源兼容性.md)
3. [render设计.md](render设计.md)
