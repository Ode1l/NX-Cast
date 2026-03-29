# Player 层设计（面向 NX-Cast）

## 1. 目标

`Player` 层的职责不是“实现某个播放器库”，而是把 `DLNA control` 和“真实播放能力”隔离开。

对 `NX-Cast` 来说，这一层必须解决四件事：

1. 接收控制层命令：`SetURI / Play / Pause / Stop / Seek / Volume / Mute`
2. 管理真实播放状态：`STOPPED / PLAYING / PAUSED / ERROR`
3. 对接实际后端：`mock / mpv / FFmpeg / Switch 平台渲染与音频`
4. 把状态变化回调给控制层，作为 `AVTransport / RenderingControl` 的真实状态源

结论先说：

1. 当前 `mock backend` 只是为了打通控制链路，不是最终播放器。
2. 播放器结构参考 `pplay` 的播放器封装方式。
3. Switch 平台后端参考 `nxmp` 的 `libmpv + hos + deko3d + hwdec` 路线。
4. 最小 FFmpeg 验证路径参考 `PlayerNX` 的解码链思路。

---

## 2. 外部参考说明

为了便于公开发布，本仓库中的设计文档只保留“参考了什么设计”，不再展开第三方项目的具体实现分析。

本模块当前采用如下参考关系：

1. `pplay`：参考其 `player` 对象封装方式，以及 `libmpv` 后端与上层控制分离的结构
2. `nxmp`：参考其 Switch 平台媒体后端思路，包括 `hos` 音频、`deko3d` 渲染、`nvtegra` 硬解码方向
3. `PlayerNX`：参考其最小 FFmpeg 解码链，用于理解和验证软件解码路径

设计结论：

1. `NX-Cast` 保留自己的 `player core + backend` 骨架
2. 播放器结构优先参考 `pplay`
3. 平台后端能力优先参考 `nxmp`
4. `FFmpeg` 路线只作为验证或备选，不作为当前主实现路线

## 3. 选型结论

`NX-Cast Player` 的正式路线应当是：

1. 播放器结构参考 `pplay`
2. Switch 平台输出与硬解码方向参考 `nxmp`
3. 最小 FFmpeg 验证路径参考 `PlayerNX`

一句话总结：

`pplay` 提供播放器封装参考，`nxmp` 提供 Switch 平台后端参考，`PlayerNX` 提供最小 FFmpeg 验证参考。

---

## 4. 当前状态

当前仓库里已经有一套最小 `player` 骨架：

```text
source/player/
  player.h
  player.c
  player_backend.h
  player_backend_mock.c
```

当前能力：

1. 有统一 `player_*` API
2. 有 `PlayerEvent` 回调机制
3. 有 `mock backend`
4. SOAP 已经可以通过 `player` 状态闭环

当前缺失：

1. 真实网络流播放
2. 音频输出
3. 视频渲染
4. 后台解码线程
5. 更细粒度状态，例如 `BUFFERING / SEEKING / ENDED`

因此，这一阶段不是“从零设计”，而是“在已有 player 骨架上升级为真实后端”。

---

## 5. 分层边界

建议的职责边界如下。

### 5.1 control 层

职责：

1. 解析 SOAP Action
2. 校验参数
3. 调用 `player_*` API
4. 把 `Player` 状态映射到 `AVTransport / RenderingControl`

不负责：

1. 拉流
2. 解码
3. 上屏
4. 音频播放

### 5.2 player core 层

职责：

1. 统一对外 API
2. 统一状态机
3. 统一事件回调
4. 统一后端选择
5. 作为控制层唯一依赖对象

不负责：

1. 具体解码库实现细节
2. 具体图形 API 调用细节

### 5.3 backend 层

职责：

1. 真正执行 `set_uri / play / pause / stop / seek`
2. 管理内部播放线程
3. 上报状态变化、进度、时长、错误

候选实现：

1. `mock`
2. `mpv`
3. `ffmpeg`

### 5.4 platform output 层

职责：

1. 音频输出
2. 视频渲染
3. 硬解码资源接入

Switch 上的主要目标是：

1. 音频：`hos` 或 libnx 音频能力
2. 视频：`deko3d`
3. 硬解码：`nvtegra` 路线

---

## 6. 推荐目录结构

当前目录可以继续保留，但建议下一阶段扩展为：

```text
source/player/
  player.h
  player.c
  player_backend.h
  player_backend_mock.c
  player_backend_mpv.c        或 player_backend_mpv.cpp
  player_backend_ffmpeg.c     (仅验证或备选)
  player_state.h              (可选，若状态定义继续扩张)
  player_platform.h           (可选，平台输出抽象)
  player_platform_dk3d.c      (后续)
  player_platform_audio.c     (后续)
```

如果后端逐渐变复杂，可以再拆子目录：

```text
source/player/
  core/
  backend/
  platform/
```

但当前阶段没有必要一开始就过度拆分。

---

## 7. 核心接口设计

当前 `player.h` 的方向是对的，应继续保留“上层只看 Facade，不感知后端”的原则。

推荐保留的最小接口：

```c
typedef enum
{
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_ERROR
} PlayerState;

bool player_init(void);
void player_deinit(void);

bool player_set_uri(const char *uri, const char *metadata);
bool player_play(void);
bool player_pause(void);
bool player_stop(void);
bool player_seek_ms(int position_ms);
bool player_set_volume(int volume_0_100);
bool player_set_mute(bool mute);

int player_get_position_ms(void);
int player_get_duration_ms(void);
int player_get_volume(void);
bool player_get_mute(void);
PlayerState player_get_state(void);
```

推荐下一阶段补充但不必立刻加的接口：

```c
bool player_set_rate(float rate);
bool player_get_media_info(PlayerMediaInfo *info);
bool player_get_last_error(PlayerError *error);
```

原则：

1. `control` 层只调用这里，不直连后端
2. 后端替换时，上层代码不改
3. `mock -> mpv -> 更深平台优化` 可以逐步演进

---

## 8. 状态机设计

当前状态枚举偏少，但作为第一版可以接受。

正式设计建议按两层理解：

### 8.1 对 SOAP 暴露的状态

这层要兼容 DLNA/UPnP 语义：

1. `STOPPED`
2. `PLAYING`
3. `PAUSED_PLAYBACK`
4. `TRANSITIONING`
5. `NO_MEDIA_PRESENT`

### 8.2 Player 内部状态

这层可以更细：

1. `IDLE`
2. `STOPPED`
3. `OPENING`
4. `BUFFERING`
5. `PLAYING`
6. `PAUSED`
7. `SEEKING`
8. `ENDED`
9. `ERROR`

建议：

1. 当前代码可先继续用简化状态
2. 真后端落地时，再把内部状态扩充
3. 控制层只看“映射后的 SOAP 状态”，不要直接依赖内部状态枚举值

---

## 9. 事件机制设计

当前 `PlayerEvent` 回调方向正确，必须保留。

原因：

1. 控制层不能猜状态
2. 进度、时长、播放结束、错误都应由 player 主动上报
3. 将来 `GENA/LastChange` 也必须依赖这套事件源

建议保留的核心事件：

1. `PLAYER_EVENT_STATE_CHANGED`
2. `PLAYER_EVENT_POSITION_CHANGED`
3. `PLAYER_EVENT_DURATION_CHANGED`
4. `PLAYER_EVENT_VOLUME_CHANGED`
5. `PLAYER_EVENT_MUTE_CHANGED`
6. `PLAYER_EVENT_URI_CHANGED`
7. `PLAYER_EVENT_ERROR`

建议未来增加但可选：

1. `PLAYER_EVENT_BUFFERING_CHANGED`
2. `PLAYER_EVENT_END_OF_STREAM`
3. `PLAYER_EVENT_VIDEO_SIZE_CHANGED`

原则：

1. 事件来自真实后端，不由控制层伪造
2. `GetPositionInfo` 可读当前值
3. `LastChange/NOTIFY` 可直接复用事件流

---

## 10. 并发与线程模型

播放器这一层不应运行在 SOAP 请求线程里。

推荐模型：

1. `SOAP/HTTP` 线程接收命令
2. `player backend` 内部有独立播放线程
3. 状态变化通过事件回调回到控制层

最小并发模型：

1. 主线程/UI 线程
2. 网络控制线程
3. 播放线程

后续可能扩展：

1. 渲染线程
2. 音频输出线程

设计原则：

1. SOAP handler 不阻塞等待长时间解码
2. `Play/Pause/Seek` 只下发命令，不自己做重活
3. 真正状态变化由后端线程异步回报

---

## 11. 后端路线选择

### 11.1 mock backend

用途：

1. 验证 SOAP 控制链路
2. 验证状态同步
3. 验证 UI 与日志

结论：

1. 必须保留
2. 但只用于调试，不是最终播放实现

### 11.2 mpv backend

这是最推荐的正式第一后端。

原因：

1. `libmpv` 已经解决大量通用播放器问题
2. 能尽快实现“真实 URL 播放”
3. 后续可逐步接入 `deko3d / hos / hwdec`

适合作为：

1. `NX-Cast` 第一版真实播放器后端
2. 中长期默认后端

### 11.3 FFmpeg backend

用途不是替代 `mpv`，而是：

1. 作为最小验证方案
2. 作为学习和调试样例
3. 作为未来特殊场景的可控底层实现

不建议把它作为当前主路线的原因：

1. 你要自己处理音视频同步
2. 你要自己处理音频输出
3. 你要自己处理缓冲、网络异常、seek、渲染
4. 实现成本明显高于 `libmpv`

结论：

1. 短期优先 `mpv`
2. `FFmpeg` 只做研究或备选

---

## 12. Switch 平台输出设计

### 12.1 音频

播放器最终要把音频数据交给 Switch 输出设备。

设计建议：

1. 初版依赖 `libmpv` 已支持的 Switch 音频输出路径
2. 后续再抽象成独立平台音频层

### 12.2 视频渲染

如果只是控制链路，当前 UI 和日志已经足够。

如果要真正显示视频：

1. 必须有视频输出层
2. 推荐方向是 `deko3d`

结论：

1. `FFmpeg` 不负责上屏
2. 真正上屏要靠图形 API 或播放器渲染上下文

### 12.3 硬解码

这不是第一阶段目标，但架构上要预留。

建议：

1. 第一阶段先让软件链路跑通
2. 第二阶段再接 `nvtegra`
3. 不要在 `player core` 中写死硬解码逻辑

---

## 13. 与 DLNA control 的对接原则

控制层和 `player` 的关系必须保持简单。

### 13.1 AVTransport

调用：

1. `SetAVTransportURI` -> `player_set_uri`
2. `Play` -> `player_play`
3. `Pause` -> `player_pause`
4. `Stop` -> `player_stop`
5. `Seek` -> `player_seek_ms`

读取：

1. `GetTransportInfo` -> `player_get_state`
2. `GetPositionInfo` -> `player_get_position_ms / player_get_duration_ms`

### 13.2 RenderingControl

调用：

1. `SetVolume` -> `player_set_volume`
2. `SetMute` -> `player_set_mute`

读取：

1. `GetVolume` -> `player_get_volume`
2. `GetMute` -> `player_get_mute`

### 13.3 ConnectionManager

这一层更多是能力声明，不直接驱动真实播放。

因此不应耦合到后端实现细节。

---

## 14. 本地控制与手机控制同步

这是 `player` 层必须支持的设计，而不是 optional 的临时功能。

原因：

1. 手机控制和本地按键控制最终都应落到同一个 `player`
2. `player` 是单一真实状态源
3. 控制层只是不同入口

同步路径：

1. 手机 SOAP 控制 -> `player`
2. 本地按键控制 -> `player`
3. `player` 统一发事件 -> 控制层状态同步

如果以后要让手机 UI 立即感知本地操作，再实现：

1. `GENA` 订阅
2. `LastChange` 事件

这部分仍可标记为 optional，但前提是 `player` 事件源必须先设计正确。

---

## 15. 推荐实施顺序

### 阶段 1：稳定当前骨架

1. 保持 `player.c / player.h / player_backend.h`
2. 继续使用 `player_backend_mock.c`
3. 确认 SOAP 状态同步完全稳定

### 阶段 2：引入真实后端

1. 新增 `player_backend_mpv.c` 或 `player_backend_mpv.cpp`
2. 先实现：
   1. `set_uri`
   2. `play`
   3. `pause`
   4. `stop`
   5. `seek`
   6. `position / duration`
3. 能真实播放网络 URL

### 阶段 3：接入 Switch 平台能力

1. 优化视频渲染
2. 优化音频输出
3. 研究 `deko3d`
4. 研究 `nvtegra`

### 阶段 4：事件与兼容性增强

1. 增加更完整状态
2. 增加 `LastChange`
3. 增加缓冲、错误、播放结束事件
4. 处理不同控制端兼容性

---

## 16. 最终结论

`NX-Cast` 的 `player` 层应保持本项目自己的实现骨架。

正确路线是：

1. 保留当前 `player core + backend ops + mock` 骨架
2. 播放器结构采用 `player core + backend` 分层
3. 平台实现按 Switch 媒体后端方式逐步增强
4. 把 `FFmpeg` 限制在验证和研究范围内

因此，下一阶段最合理的落地目标不是“自己从零写完整 FFmpeg 播放器”，而是：

1. 先做 `player_backend_mpv`
2. 先让真实网络媒体可播
3. 再逐步接入 `deko3d / hos / hwdec`

这条路线风险最低，也最符合当前 `NX-Cast` 的工程阶段。
