# Player 层设计（面向 NX-Cast）

## 1. 目标

`Player` 层的目标不是绑定某个具体播放器库，而是提供一套稳定、可替换、可扩展的播放控制内核，使上层 `DLNA control` 只依赖统一接口，不依赖具体解码与渲染实现。

这一层需要解决四件事：

1. 接收控制命令：`SetURI / Play / Pause / Stop / Seek / Volume / Mute`
2. 维护真实播放状态：位置、时长、音量、静音、错误、播放状态
3. 驱动具体播放后端：`mock / mpv / FFmpeg / 平台输出`
4. 把状态变化主动回推给控制层，作为 `AVTransport / RenderingControl` 的真实状态源

---

## 2. 设计原则

本模块采用以下原则：

1. 上层协议不碰解码细节
2. 控制命令与真实播放线程解耦
3. 播放状态只有一个真实来源，即 `player`
4. 后端可替换，但上层 API 不变化
5. 事件必须由真实状态驱动，不能由 SOAP 层伪造

这意味着：

1. `SOAP` 只做协议解析和参数校验
2. `player` 只做播放状态与后端协调
3. `backend` 只做实际播放
4. `platform output` 只做音频、视频和硬件资源接入

---

## 3. 采用的模式

本模块不是“照着某个项目写”，而是明确采用以下通用模式。

### 3.1 Facade 模式

对外统一暴露 `player_*` API，由 `player.c` 作为唯一入口。

当前对应：

1. [player.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player.h)
2. [player.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player.c)

作用：

1. `control` 层只依赖 `player_*`
2. 后端替换时，SOAP 层无需修改
3. UI、本地按键、远程控制都走同一套入口

### 3.2 Strategy 模式

具体播放实现通过 `PlayerBackendOps` 抽象为可替换策略。

当前对应：

1. [player_backend.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player_backend.h)
2. [player_backend_mock.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player_backend_mock.c)

核心思想：

1. `player` 只知道“后端有这些能力”
2. `mock / mpv / FFmpeg` 只是不同策略实现
3. 切换后端不改变上层控制逻辑

### 3.3 Observer 模式

播放状态变化通过事件回调主动通知外部，而不是让控制层自己猜。

当前对应：

1. `PlayerEvent`
2. `PlayerEventCallback`
3. `player_set_event_callback()`

作用：

1. `Play/Pause/Seek/Stop` 的真实结果由后端回报
2. `AVTransport` 状态同步有统一事件源
3. 以后 `GENA / LastChange` 可直接复用

### 3.4 状态机模式

播放器状态不能靠零散布尔变量拼出来，而必须由有限状态机驱动。

当前状态还是简化版，但方向已经确定：

1. `IDLE`
2. `STOPPED`
3. `PLAYING`
4. `PAUSED`
5. `ERROR`

后续应扩展：

1. `OPENING`
2. `BUFFERING`
3. `SEEKING`
4. `ENDED`

### 3.5 异步工作线程模式

播放器重操作不能阻塞 SOAP 请求线程，因此正式后端必须采用独立播放线程。

当前情况：

1. `mock backend` 仍是同步模拟
2. 正式后端应采用异步播放线程

推荐模型：

1. SOAP 线程只下发命令
2. backend 线程执行打开、播放、seek、停止
3. 事件回调把结果送回控制层

### 3.6 平台适配层模式

解码、音频输出、视频渲染、硬解码资源不应全部塞进 `player core`。

因此平台相关能力应单独抽象：

1. 音频输出层
2. 视频渲染层
3. 硬解码接入层

这样做的目的：

1. `player core` 保持平台中立
2. Switch 平台能力可以逐步增强
3. 后续替换渲染路径时不会影响 SOAP 和状态机

---

## 4. 当前代码映射

当前仓库中已经存在最小可用骨架：

```text
source/player/
  player.h
  player.c
  player_backend.h
  player_backend_mock.c
```

代码角色如下：

1. `player.h`
   作用：Facade 接口定义，供上层调用
2. `player.c`
   作用：Facade 实现，负责转发到当前 backend，并管理事件回调
3. `player_backend.h`
   作用：Strategy 接口定义，约束后端能力集合
4. `player_backend_mock.c`
   作用：第一个 backend，实现最小状态闭环与日志验证

当前已实现能力：

1. `player_*` 统一接口
2. 事件回调机制
3. 基础状态读写
4. SOAP 到 player 的控制闭环

当前未实现能力：

1. 真实网络流拉取
2. 真实音频输出
3. 真实视频渲染
4. 后端独立播放线程
5. 更完整状态机

---

## 5. 分层边界

### 5.1 control 层

职责：

1. 解析 SOAP Action
2. 校验参数
3. 调用 `player_*`
4. 把 `player` 状态映射到 `AVTransport / RenderingControl`

不负责：

1. 拉流
2. 解码
3. 上屏
4. 音频播放

### 5.2 player core 层

职责：

1. 统一对外接口
2. 管理状态机
3. 管理事件回调
4. 选择和调度 backend

不负责：

1. 具体解码库调用
2. 图形 API 细节
3. 音频设备细节

### 5.3 backend 层

职责：

1. 执行 `set_uri / play / pause / stop / seek`
2. 查询位置、时长、音量、静音
3. 驱动解码与播放线程
4. 上报状态变化和错误

### 5.4 platform output 层

职责：

1. 音频输出
2. 视频渲染
3. 硬解码接入

---

## 6. 推荐目录结构

当前目录结构可以保留，并按以下方向扩展：

```text
source/player/
  player.h
  player.c
  player_backend.h
  player_backend_mock.c
  player_backend_mpv.c
  player_backend_ffmpeg.c
  player_state.h
  player_platform.h
  player_platform_audio.c
  player_platform_video.c
```

说明：

1. `player_backend_mpv.c`
   作为第一版真实播放后端
2. `player_backend_ffmpeg.c`
   仅作为验证或研究路径
3. `player_platform_*`
   抽象 Switch 的音频、视频与硬件能力

如果后端继续增长，再拆成 `core / backend / platform` 子目录。

---

## 7. 对外接口设计

`player` 对外接口保持尽量小，但必须完整覆盖 DMR 控制需求。

当前最小接口：

```c
bool player_init(void);
void player_deinit(void);

void player_set_event_callback(PlayerEventCallback callback, void *user);

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

接口约束：

1. `SetURI` 只负责切换媒体，不自动播放
2. `Play` 只在当前 URI 已存在时有效
3. `Pause` 只对 `PLAYING` 有意义
4. `Seek` 采用毫秒作为内部统一单位
5. `Volume` 使用 `0..100`

后续可选扩展：

1. `player_set_rate()`
2. `player_get_media_info()`
3. `player_get_last_error()`

---

## 8. 状态机设计

播放器状态分为两层：内部状态与对外状态。

### 8.1 内部状态

建议正式采用如下状态：

1. `IDLE`
2. `STOPPED`
3. `OPENING`
4. `BUFFERING`
5. `PLAYING`
6. `PAUSED`
7. `SEEKING`
8. `ENDED`
9. `ERROR`

### 8.2 对 SOAP 暴露的状态

对 `AVTransport` 暴露时，需要映射到 UPnP 语义：

1. `NO_MEDIA_PRESENT`
2. `STOPPED`
3. `TRANSITIONING`
4. `PLAYING`
5. `PAUSED_PLAYBACK`

映射原则：

1. `IDLE` -> `NO_MEDIA_PRESENT`
2. `STOPPED` -> `STOPPED`
3. `OPENING / BUFFERING / SEEKING` -> `TRANSITIONING`
4. `PLAYING` -> `PLAYING`
5. `PAUSED` -> `PAUSED_PLAYBACK`
6. `ENDED` -> `STOPPED`
7. `ERROR` -> 由 SOAP fault 或错误状态处理

---

## 9. 事件模型

事件是 `player` 与 `control` 之间的唯一真实同步机制。

当前保留的事件类型：

1. `PLAYER_EVENT_STATE_CHANGED`
2. `PLAYER_EVENT_POSITION_CHANGED`
3. `PLAYER_EVENT_DURATION_CHANGED`
4. `PLAYER_EVENT_VOLUME_CHANGED`
5. `PLAYER_EVENT_MUTE_CHANGED`
6. `PLAYER_EVENT_URI_CHANGED`
7. `PLAYER_EVENT_ERROR`

推荐增加的事件：

1. `PLAYER_EVENT_BUFFERING_CHANGED`
2. `PLAYER_EVENT_END_OF_STREAM`
3. `PLAYER_EVENT_VIDEO_SIZE_CHANGED`

事件规则：

1. 事件必须来自后端真实状态变化
2. 事件不能由 SOAP 层假定生成
3. 进度事件可以定时发送
4. 状态切换事件必须在状态真正变化后发送

---

## 10. 并发模型

正式后端应采用单后端线程模型，而不是“每个命令自己起线程”。

推荐线程模型：

1. UI 主线程
2. SOAP/HTTP 网络线程
3. Player backend 播放线程

后续可选：

1. 渲染线程
2. 音频输出线程

采用单 backend 线程的原因：

1. 状态一致性更好
2. `seek/play/pause/stop` 不会相互打架
3. 更适合用命令队列串行处理播放命令

推荐命令流：

1. SOAP 收到 `Play`
2. `player_play()` 把命令交给 backend
3. backend 线程执行
4. backend 更新状态并发事件
5. control 层根据事件刷新 `AVTransport`

---

## 11. 后端选择

### 11.1 mock backend

用途：

1. 验证协议控制链路
2. 验证状态同步
3. 验证日志系统

结论：

1. 必须保留
2. 只用于测试，不负责真实播放

### 11.2 mpv backend

这是下一阶段的主后端。

原因：

1. 已具备成熟的网络媒体播放能力
2. 已处理大量播放器共性问题，如解码、同步、缓存、seek
3. 更适合尽快把 URL 播放跑通
4. 后续能继续对接 Switch 平台输出优化

结论：

1. 第一版真实播放后端优先做 `mpv`
2. 未来默认后端也优先考虑 `mpv`

### 11.3 FFmpeg backend

定位：

1. 学习和验证路径
2. 研究更底层的播放控制
3. 备选后端

不作为主路线的原因：

1. 需要自行处理音视频同步
2. 需要自行处理音频输出
3. 需要自行处理渲染与缓冲
4. 研发成本更高

结论：

1. 只作为验证或研究方向
2. 当前不作为第一优先级

---

## 12. Switch 平台输出设计

### 12.1 音频输出

目标：

1. 把解码后的音频稳定送到 Switch 输出设备

设计要求：

1. 音频输出不写死在 `player core`
2. 后续可独立替换或优化

### 12.2 视频渲染

目标：

1. 把视频帧稳定显示到屏幕

设计要求：

1. 渲染逻辑不写死在控制层
2. 渲染能力由平台输出层承担
3. 后续应预留 `deko3d` 路线

### 12.3 硬解码

目标：

1. 在保持架构稳定的前提下，后续接入硬件能力

设计要求：

1. 不在 `player core` 中写死硬解码判断
2. 硬解码应作为 backend 或 platform 能力接入

---

## 13. 与 DLNA control 的对接

### 13.1 AVTransport

命令映射：

1. `SetAVTransportURI` -> `player_set_uri`
2. `Play` -> `player_play`
3. `Pause` -> `player_pause`
4. `Stop` -> `player_stop`
5. `Seek` -> `player_seek_ms`

查询映射：

1. `GetTransportInfo` -> `player_get_state`
2. `GetPositionInfo` -> `player_get_position_ms / player_get_duration_ms`

### 13.2 RenderingControl

命令映射：

1. `SetVolume` -> `player_set_volume`
2. `SetMute` -> `player_set_mute`

查询映射：

1. `GetVolume` -> `player_get_volume`
2. `GetMute` -> `player_get_mute`

### 13.3 ConnectionManager

职责：

1. 声明能力
2. 不直接驱动真实播放

因此它不应依赖具体后端实现细节。

---

## 14. 本地控制与远程控制同步

`player` 必须是唯一真实状态源。

这意味着：

1. 手机 SOAP 控制走 `player`
2. 本地按键控制也走 `player`
3. 两者共享同一份状态机
4. 所有外部状态展示都来自同一事件流

这样做的好处：

1. 不会出现本地控制和手机控制各自维护状态
2. 以后做 `GENA / LastChange` 时可以直接复用 `player` 事件

---

## 15. 实施顺序

### 阶段 1：稳定现有骨架

1. 保留 `player.c / player.h / player_backend.h`
2. 继续使用 `player_backend_mock.c`
3. 确认 SOAP 和 player 状态同步完全稳定

### 阶段 2：引入真实后端

1. 新增 `player_backend_mpv.c`
2. 先实现：
   1. `set_uri`
   2. `play`
   3. `pause`
   4. `stop`
   5. `seek`
   6. `position / duration`
3. 先完成“真实 URL 可播放”

### 阶段 3：接入平台输出

1. 补音频输出
2. 补视频渲染
3. 为 `deko3d` 和硬解码预留结构

### 阶段 4：增强状态与事件

1. 引入 `BUFFERING / SEEKING / ENDED`
2. 增加更多事件
3. 对接 `LastChange`

---

## 16. 结论

`NX-Cast player` 的正式设计已经明确：

1. 对外使用 Facade
2. 对后端使用 Strategy
3. 对状态同步使用 Observer
4. 对播放状态使用状态机
5. 对重操作使用异步 backend 线程
6. 对平台能力使用独立适配层

因此下一步最合理的实现目标不是继续扩充 `mock`，而是：

1. 增加 `player_backend_mpv`
2. 保持现有 `player core` 不变
3. 先打通真实网络 URL 播放

外部项目致谢放在 `README`，本设计文档只描述 `NX-Cast` 自身采用的技术方案。
