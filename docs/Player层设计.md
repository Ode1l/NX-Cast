# Player 层设计

本文档记录 `NX-Cast` 当前 `player` 层的真实结构，不再保留已经删除的旧播放前处理设计。

## 1. 目标

`player` 层当前解决四件事：

1. 接收统一播放命令
2. 维护运行时播放状态
3. 驱动具体播放后端
4. 向协议层提供稳定的 snapshot 和 event

它的目标不是：

1. 直接承载协议逻辑
2. 自己实现一套源建模流水线
3. 在 `player` 内再拆一层独立打开策略系统

## 2. 当前边界

当前分层：

```text
player
  -> core
  -> backend
  -> render
```

### 2.1 core

当前职责：

1. 维护 session
2. 缓存 snapshot
3. 转发 backend event
4. 维护当前媒体和字符串所有权
5. 驱动 event pump thread

核心文件：

- `source/player/core/session.c`
- `source/player/types.h`
- `source/player/types.c`
- `source/player/renderer.h`

### 2.2 backend

当前职责：

1. 真实播放
2. 与 `libmpv` 对接
3. 命令直发
4. 属性观察和事件回收
5. 回推统一 `PlayerEvent`

核心文件：

- `source/player/backend/libmpv.c`
- `source/player/backend/mock.c`

### 2.3 render

当前职责：

1. 前台视频页切换
2. render context 生命周期
3. 与 backend 的窄 render 接口对接

核心文件：

- `source/player/render/view.c`
- `source/player/render/frontend.c`

## 3. 当前公共接口

`player` 对外只暴露三类东西：

1. 命令
2. `PlayerSnapshot`
3. `PlayerEvent`

### 3.1 命令

包括：

1. `set uri`
2. `set media`
3. `play`
4. `pause`
5. `stop`
6. `seek`
7. `volume`
8. `mute`

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

其中 `PlayerMedia` 的长字符串已经改为动态 `char *` 所有权模型。

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

## 4. 当前播放模型

当前播放器链路刻意保持很薄：

```text
protocol action
  -> renderer_set_uri / play / pause / seek / stop
  -> backend/libmpv command
  -> libmpv runtime events
  -> PlayerEvent / PlayerSnapshot
  -> protocol_state sync
```

`SetAVTransportURI` 本身不再经过中间建模流水线，而是直接把 URL 交给 renderer，再由 `libmpv` 负责探测、拉流、demux 和 decode。

## 5. 当前 libmpv 对接方式

当前 `libmpv` 对接采用两条线：

1. 命令线
   - `loadfile`
   - `pause`
   - `seek`
   - `set_property`
   - `stop`
2. 观察线
   - `time-pos`
   - `duration`
   - `pause`
   - `mute`
   - `seekable`
   - `idle-active`
   - `paused-for-cache`
   - `seeking`
   - EOF / error 事件

所以当前模型不是“协议层自己维护播放状态”，而是“协议层下发控制，renderer 再把 `libmpv` 的真实运行时状态同步回来”。

## 6. 当前 render/backend 路线

当前已落地路线：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

当前未真正落地：

1. explicit `nvtegra` hwdec backend
2. `deko3d` 渲染路径

## 7. 当前工作重点

当前 player 主线不是继续扩层，而是：

1. 继续加固 `libmpv` 事件同步
2. 保持 renderer 状态与协议状态一致
3. 在真实控制端和真实 URL 上做稳定性验证
4. 为未来工具链升级保留清晰 backend 边界
