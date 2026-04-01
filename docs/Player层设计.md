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

对外统一暴露 `player_*` API，由 `core/session.c` 作为唯一入口实现。

当前对应：

1. [player.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player.h)
2. [session.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/core/session.c)

作用：

1. 协议/控制层只依赖 `player_*`
2. 后端替换时，SOAP 层无需修改
3. UI、本地按键、远程控制都走同一套入口

### 3.2 Strategy 模式

具体播放实现通过 `BackendOps` 抽象为可替换策略。

当前对应：

1. [backend.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/backend.h)
2. [mock.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/backend/mock.c)

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

当前仓库中已经存在的核心骨架：

```text
source/player/
  player.h
  types.h
  backend.h
  ingress.h
  policy.h
  view.h
  core/
    session.c
  backend/
    mock.c
    libmpv.c
  render/
    view.c
    frontend.c
    internal.h
  ingress/
    ingress.c
    policy_default.c
    policy_hls.c
    policy_vendor.c
```

代码角色如下：

1. `player.h`
   作用：Facade 接口定义，供上层调用
2. `core/session.c`
   作用：当前 `player core` 实现，负责 owner thread、命令队列、snapshot 与事件转发
3. `backend.h`
   作用：Strategy 接口定义，约束后端能力集合
4. `backend/mock.c`
   作用：第一个 backend，实现最小状态闭环与日志验证
5. `backend/libmpv.c`
   作用：当前真实播放 backend，已接入 `libmpv`，但仍是“同步适配器 + 最小状态机”，还不是正式 `Player Core`
6. `ingress/`
   作用：player 入口层，负责 `URI + metadata` 解析、候选资源选择、格式识别和策略注入
7. `render/`
   作用：player 前台视图与平台显示层，负责日志页/视频页切换和前台 render loop

当前已实现能力：

1. `player_*` 统一接口
2. 事件回调机制
3. `mock` 与 `libmpv` 双 backend
4. SOAP 到 player 的控制闭环
5. `https mp4 / hls / header-sensitive URL` 的初步打开实验能力
6. `ingress -> PlayerMedia -> backend set_media` 的第一阶段边界已经接入

当前未实现能力：

1. `policy_*` 仍是第一版，细粒度来源策略还不完整
2. backend 内仍保留部分 runtime overrides
3. 真实视频渲染与平台输出
4. 更完整的网络流状态机
5. player 入口的候选资源选择仍未独立设计

这里需要特别说明：

1. “独立入口策略分层”现在已经落地到 `ingress/policy_*`
2. 但当前还只是第一阶段
3. 目前只是把 `PlayerMedia` 公共化，并接到了 backend 边界
4. 还没有把所有来源策略都完全从 `libmpv backend` 中迁移出去
5. 基于 `CurrentURIMetaData` `DIDL-Lite res/protocolInfo` 的最终候选资源选择，应归到 player 入口 / `ingress`，不归到 `ConnectionManager` 内部硬编码

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
  types.h
  backend.h
  ingress.h
  policy.h
  view.h
  core/
    session.c
  backend/
    mock.c
    libmpv.c
    ffmpeg.c
  ingress/
    ingress.c
    policy_default.c
    policy_hls.c
    policy_vendor.c
  render/
    audio.c
    view.c
    frontend.c
    internal.h
```

说明：

1. `libmpv.c`
   作为当前真实播放后端
2. `backend/ffmpeg.c`
   仅作为验证或研究路径
3. `ingress/*`
   作为 player 入口、metadata 资源选择、格式识别和兼容策略层
4. `render/*`
   抽象 Switch 的前台显示、视频页和硬件相关 render 入口

当前已经收敛为 `core / backend / render / ingress` 四层，后续继续增长时优先沿这四层拆分，不再回到单目录堆叠。

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

当前实现状态补充：

1. Step 1 已给 `libmpv backend` 加入最小互斥保护
2. 但 `player` 还没有真正独立的 backend 线程
3. 当前 `player_*` 仍在调用者线程里直接执行 `libmpv` 控制
4. 当前还没有命令队列、事件队列和专门的 `mpv` 事件循环

因此当前结论应理解为：

1. 现在已经有“基础并发保护”
2. 但还不能视为“正式并发模型已经完成”
3. 真正完整的线程模型仍属于 Step 3 范围

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

### Step 1：真实播放控制后端

目标：

1. 先把 `player` 从 `mock` 接成真实 `libmpv backend`
2. 先解决“真的能播、真的能控制、真的能返回状态”
3. 暂时不把“视频如何显示得漂亮”与“播放控制后端”绑死在一起

这一阶段必须补充并明确的设计：

1. 必须：backend 选择策略
   1. 默认后端是否为 `AUTO -> libmpv`
   2. 失败时是否回退到 `mock`
2. 必须：`libmpv` 命令映射
   1. `player_set_uri` -> `loadfile` 或等价逻辑
   2. `player_play`
   3. `player_pause`
   4. `player_stop`
   5. `player_seek_ms`
3. 必须：状态读取映射
   1. `position`
   2. `duration`
   3. `pause`
   4. `volume`
   5. `mute`
4. 必须：错误处理策略
   1. `loadfile` 失败怎么映射
   2. 无 URI 时 `play` 如何返回
   3. backend 初始化失败时如何处理
5. 必须：事件同步策略
   1. 哪些状态变化立即发 `PlayerEvent`
   2. 哪些查询使用快照读取

这一阶段 optional：

1. optional：播放速率
2. optional：多音轨/字幕轨切换
3. optional：更细粒度错误码

Step 1 的通过标准：

1. `SetURI / Play / Pause / Stop / Seek` 已接入真实 `libmpv`
2. `GetPositionInfo / GetTransportInfo / GetVolume / GetMute` 返回真实值
3. SOAP 层不再只依赖 `mock`
4. 但这一步仍不代表网络流状态机已经正式完成

### Step 2：渲染与前台显示设计

目标：

1. 解决“视频如何真正显示到屏幕上”
2. 明确播放时 UI 与日志界面的切换方式

这一阶段必须补充并明确的设计：

1. 必须：前台显示权归属
   1. 播放视频时是否继续保留当前 `console` 日志界面
   2. 停止播放后是否切回日志界面
2. 必须：渲染生命周期
   1. 谁创建 render context
   2. 谁驱动每帧 render
   3. render 资源何时初始化和释放
3. 必须：`libmpv render API` 的接入边界
   1. `player backend` 是否直接持有 render context
   2. 还是由平台显示层持有
4. 必须：日志 UI 切换策略
   1. 播放时隐藏日志
   2. 播放时叠加日志
   3. 播放时完全切换到视频画面
5. 必须：最小显示目标
   1. Step 2 是否必须显示视频
   2. 还是先只保证音频与播放状态

这一阶段 optional：

1. optional：视频显示时叠加调试信息
2. optional：播放中快速切换日志/视频界面
3. optional：OSD 样式

Step 2 的通过标准：

1. 播放视频时屏幕行为已定义清楚
2. 至少有一条可运行的视频显示路径
3. 日志 UI 与视频显示不会互相抢占导致异常

#### Step 2 的阶段拆分

##### Step 2.1 前台显示权与 render loop 骨架

先完成：

1. 明确前台显示权由主线程 / UI 层持有
2. `player backend` 不直接持有 render context
3. `player_view` 只负责平台显示抽象，由主线程每帧驱动
4. 先建立“日志视图 / 视频视图占位”的前台切换骨架

这一阶段的目标不是马上把真实视频画面打出来，而是先把 ownership 和 render loop 位置固定。

##### Step 2.2 `libmpv render API` 最小接入

再完成：

1. 把真实 render target 接到 `player_view`
2. 接通 `libmpv render API`
3. 让前台每帧 render 能显示真实视频
4. 当前最小落地先使用 `software render + libnx framebuffer`，不在这一步直接引入 `EGL/deko3d`

##### Step 2.3 平台显示完善

最后完成：

1. 日志 UI 与视频 UI 切换策略
2. 屏幕接管
3. `deko3d` 路径
4. 调试叠加层和播放态 UI

#### Step 2.1 当前先固定的边界

当前实现先固定下面这组边界：

1. 前台显示权归 `main` 所在主线程，不归 `player backend`
2. `player backend` 继续只负责播放控制、状态与事件，不创建 render context
3. `player_view` 作为 Step 2 的平台显示适配层，接收 `PlayerSnapshot` 并决定前台视图
4. 主线程每帧调用 platform video 的 begin-frame / render-frame 骨架
5. 真正的 `libmpv render API` 接入放到 Step 2.2

#### Step 2.2 当前最小落地

当前实现采用下面这条最小可运行路径：

详细路线讨论见 [render设计.md](./render设计.md)。

1. `player backend/libmpv` 提供窄接口，只暴露 `player_video_attach_sw / player_video_render_sw / player_video_detach`
2. `player_view` 在主线程持有前台显示权，并负责接管/释放 `libnx framebuffer`
3. 前台保持“日志页 / 视频页”切换
4. 视频页进入时退出 `console`，改用 `libnx framebuffer`
5. 每帧通过 `libmpv render API` 的 software backend 渲染到 `PIXEL_FORMAT_RGBX_8888` framebuffer
6. 退出视频页后关闭 framebuffer，并恢复 `console` 日志页

这样先保证：

1. `Step 2` 的“真实视频可见”路径成立
2. `player backend` 仍然不持有平台 render target
3. 后续 `Step 2.3` 仍可把 software 路径替换成 `EGL/deko3d` 或更正式的 GPU 路径

#### 当前代码目录分层

当前 `source/player` 代码目录先按下面四层收敛：

1. `core/`
   1. owner thread
   2. 命令队列
   3. public player facade
2. `backend/`
   1. `libmpv`
   2. `mock`
3. `render/`
   1. 平台显示状态
   2. 前台视频页
   3. framebuffer/front-end
4. `ingress/`
   1. media resolve
   2. metadata 资源选择
   3. policy 注入

头文件当前仍保留在 `source/player` 根目录，作为 player 层的稳定接口面。

命名与参考原则：

1. 命名和接口层面，优先参考 `wiliwili` 和 `pplay`
2. 平台渲染细节，优先参考 `nxmp`
3. 目录已经表达层级语义时，文件名和内部接口不再重复写整段前缀

### Step 3：线程、事件与平台增强

目标：

1. 解决 backend 线程模型
2. 解决 `mpv` 事件驱动模式
3. 为 `deko3d`、硬解码和更完整状态机预留结构

这一阶段必须补充并明确的设计：

1. 必须：backend 线程模型
   1. 是否单独后台线程
   2. 是否命令队列串行处理
2. 必须：`mpv` 事件处理方式
   1. `mpv_wait_event`
   2. 或 `wakeup callback`
3. 必须：事件线程安全
   1. `PlayerEvent` 从哪个线程发出
   2. `SOAP` 读状态时如何避免竞争
4. 必须：更完整状态机
   1. `OPENING`
   2. `BUFFERING`
   3. `SEEKING`
   4. `ENDED`
5. 必须：播放与渲染的协作边界
   1. backend 负责什么
   2. platform render 负责什么

这一阶段 optional：

1. optional：`deko3d` 路径
2. optional：硬解码路径
3. optional：更高级事件通知
4. optional：`LastChange / GENA`

Step 3 的通过标准：

1. 后端线程模型明确且可运行
2. `mpv` 状态变化能稳定同步到 `player`
3. 后续 `deko3d / hwdec / LastChange` 有明确接入点

### 15.1 下一步要补的标准化设计

当前实现已经回到正确方向，但还不是完整的标准播放器状态机。后续应继续补下面这些设计。

#### 必须补

1. `PlayerState` 增加更细状态

建议至少增加：

1. `LOADING`
2. `BUFFERING`

原因：

1. 现在只有 `STOPPED / PLAYING / PAUSED / ERROR`
2. 对本地文件还勉强够用
3. 对 `https/mp4`、`m3u8`、直播流不够

如果不补这层，后续仍然会出现：

1. 加载中被误看成 `STOPPED`
2. 缓冲中被误看成 `PLAYING`

#### 必须补

2. 内部状态映射到 SOAP 的 `TRANSITIONING`

当前 SOAP 对外仍主要暴露：

1. `STOPPED`
2. `PLAYING`
3. `PAUSED_PLAYBACK`

更标准的做法是：

1. `LOADING / BUFFERING / SEEKING`
2. 对外优先映射成 `TRANSITIONING`

这样控制端不会过早把媒体当成：

1. 已稳定播放
2. 已经可以拖动

#### 必须补

3. 观察更多 `mpv` 属性

当前还应逐步纳入：

1. `seekable`
2. `paused-for-cache`
3. `core-idle`
4. `playback-abort`
5. `eof-reached`
6. `seeking`

这些属性的作用分别是：

1. 判断是否可 seek
2. 判断是否在缓存暂停
3. 判断核心是否空闲
4. 判断播放是否被中止
5. 判断是否已经到流尾
6. 判断是否处于 seek 过程

没有这些属性，网络流状态机仍然不够稳。

#### Optional

4. 为状态机引入 `ready` 与 `seekable` 分离判断

更标准的内部模型应把下面两个概念分开：

1. `media_ready`
2. `media_seekable`

原因：

1. 一个媒体可能已经打开成功，但暂时不可 seek
2. 特别是直播流和某些 HLS 流

这样后面：

1. `Play / Pause`
2. `Seek`

就可以各自按不同前提判断，而不是共用同一条件。

#### Optional

5. 冒烟测试脚本拆成“基础控制”和“seek 能力”两类

当前 `dlna_soap_smoke.sh` 仍然是一个串行脚本。后续更标准的方式是拆成：

1. 基础控制测试
   只测 `SetURI / Play / Pause / Stop / GetTransportInfo / GetPositionInfo`
2. Seek 能力测试
   只在 `seekable=true` 时执行

这样测试结果不会再被混淆成：

1. 播放成功但 seek 失败
2. 或者加载中尚未 ready 就被拿去测 seek

#### 实现参考

这组状态观测点不是凭空添加的，和现有 Switch 端播放器项目的做法是一致的：

1. `nxmp` 会在 `FILE_LOADED` 后继续观察 `paused-for-cache`、`demuxer-cache-duration`、`demuxer-cache-state`
2. `wiliwili` 会长期观察 `core-idle`、`paused-for-cache`、`playback-abort`、`seeking`、`cache-speed`
3. 两者都会把 `START_FILE`、缓存等待、seek 过程视为过渡态，而不是直接视为 `PLAYING`
4. 对 HLS 场景，额外记录 `current-demuxer`、`stream-open-filename`、`stream-path` 这类“当前到底在打开什么流”的信息是必要的
5. `trace` 级别日志只适合排查窗口，不能长期作为默认运行配置，否则会反向放大 HLS 首播延迟
6. 当 HLS 已经能播、但首播长期卡在 `LOADING` 时，优先检查 `lavf probe` 策略是否过重；这类问题通常更适合先调 `demuxer-lavf-probe-info`，而不是盲目继续增大日志级别

因此 `NX-Cast` 后续把：

1. `LOADING / BUFFERING / SEEKING`
2. `TRANSITIONING`
3. `paused-for-cache / core-idle / playback-abort / seeking`

纳入正式状态机，是对已有成熟实现的收敛，不是额外发明一套新模型。


---

## 16. 结论

`NX-Cast player` 的正式设计已经明确：

1. 对外使用 Facade
2. 对后端使用 Strategy
3. 对状态同步使用 Observer
4. 对播放状态使用状态机
5. 对重操作使用异步 backend 线程
6. 对平台能力使用独立适配层

因此下一步最合理的实现目标不再是“新增一个真实 backend”，而是：

1. 保留当前 `backend/libmpv.c`
2. 把它从“同步适配器”升级成真正的 `Player Core + event loop`
3. 同时把来源兼容逻辑从 backend 拆到独立 `ingress + policy`

关于这一点的正式方案，见：

1. [通用DLNA-DMR源适配设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/通用DLNA-DMR源适配设计.md)

外部项目致谢放在 `README`，本设计文档只描述 `NX-Cast` 自身采用的技术方案。

---

## 附录：关于当前设计边界的问答

### 问题

“你的意思是不是 现在md的内容足够用mpv播放了，但是不支持setURL play 等基础操作，更不支持真实显示并渲染，需要设计渲染周期，也需要设计log ui切换 还要设计libmpv render API和deko3d 路径的问题。还有backend 线程与 mpv 事件切换问题”

### 回答

对，基本就是这个意思，但边界要更精确地区分。

#### 1. 现在已经足够的部分

当前文档和代码，已经足够支持“第一版真实播放控制后端”的设计与实现，也就是：

1. `SetURI`
2. `Play`
3. `Pause`
4. `Stop`
5. `Seek`
6. `GetPositionInfo`
7. `GetTransportInfo`
8. `GetVolume / SetVolume`
9. `GetMute / SetMute`

这里的意思是：

1. 架构已经足够承载这些功能
2. 不是说这些功能已经用 `libmpv` 实现好了
3. 而是现在可以开始把这些基础操作接到 `libmpv`

#### 2. 现在还不够的部分

以下内容仍然需要继续设计，不能在实现中含糊处理。

##### 2.1 真实显示与渲染

这部分还没有最终定清。

原因：

1. 当前 [main.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/main.c) 仍然使用控制台日志 UI
2. 还没有正式的视频渲染接管路径

因此必须补充：

1. 播放时谁接管屏幕
2. 停止后是否切回日志 UI
3. `libmpv render API` 由谁持有和驱动
4. 是否立即接入 `deko3d`

##### 2.2 `libmpv render API` 与 `deko3d` 路径

这部分也没有最终确定。

必须先回答：

1. 第一版是否只追求“真实播放控制”
2. 还是第一版就要求“真实显示视频”
3. 如果要求显示视频，是：
   1. 先走最小显示路径
   2. 还是直接接 `deko3d`

##### 2.3 backend 线程与 mpv 事件模型

这一部分同样必须补充。

需要明确：

1. 使用 `mpv_wait_event`
2. 还是使用 `wakeup callback`
3. `PlayerEvent` 从哪个线程发出
4. `Seek / Play / Pause` 与状态查询如何保证线程安全
5. backend 是否采用命令队列

#### 3. 不要混淆的两件事

“真实播放控制”和“真实视频显示”不是同一个完成门槛。

##### 3.1 可以先做的

现在完全可以先实现：

1. `libmpv` 初始化
2. `loadfile`
3. `pause / resume / stop`
4. `seek`
5. 读取 `duration / playback-time`
6. 读取 `pause / volume / mute`
7. 同步回 `SOAP`

即使暂时还没有把视频画面完全集成到前台显示里，这一版也已经属于“真实播放器后端”。

##### 3.2 后续再做的

然后再单独解决：

1. `render context`
2. `deko3d`
3. 控制台 UI 与视频 UI 切换
4. 播放态的屏幕接管

#### 4. 当前最合理的拆分

##### 第一阶段

先实现“真实 `libmpv backend`”：

1. 支持 `SetURI / Play / Pause / Stop / Seek`
2. 支持进度、时长、音量、静音读取
3. 支持状态同步到 `SOAP`

##### 第二阶段

已落地“`Player Core + event loop` 基础骨架”：

1. `core/session.c` 独立 owner 线程
2. 命令队列串行执行播放控制
3. `mpv_wait_event` 持续事件循环
4. `PlayerSnapshot` 作为 SOAP 的稳定只读快照

##### 第三阶段

再设计并实现“显示与渲染”：

1. `libmpv render API`
2. 屏幕接管
3. 日志 UI 切换
4. `deko3d` 路径

#### 5. 最终结论

结论可以简化为：

1. 当前设计已经足够开始实现基础 `libmpv` 播放控制
2. 但还不足以无歧义地实现真实视频显示与渲染
3. 现在播放控制、线程模型和事件模型已经有正式骨架
4. 后续应单独设计：
   1. 渲染周期
   2. log UI 切换
   3. `libmpv render API`
   4. `deko3d`
