# Player 层设计（面向 NX-Cast）

## 1. 目标

把控制层与具体播放实现解耦：

1. 控制层只负责协议（SOAP/路由/参数/Fault）
2. Player 层负责真正播放（拉流、解码、渲染、进度、音量、静音）
3. 两层通过清晰接口通信

---

## 2. 分层边界

1. `control` 层
- 输入：SOAP Action
- 输出：调用 Player API

2. `player` 层
- 输入：`set_uri/play/pause/stop/seek/set_volume/set_mute`
- 输出：播放事件回调（状态、进度、错误）

3. `backend` 层（可插拔）
- 可选实现：`ffmpeg` / 平台原生解码 / mock

---

## 3. 目录建议

```text
source/player/
  player.h
  player.c
  player_backend.h
  player_backend_mock.c
  player_backend_ffmpeg.c     (后续)
```

---

## 4. 推荐接口（最小）

```c
typedef enum {
    PLAYER_STATE_IDLE,
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

int  player_get_position_ms(void);
int  player_get_duration_ms(void);
PlayerState player_get_state(void);
```

---

## 5. 与 SOAP 的对接原则

1. SOAP handler 不直接做解码逻辑
2. handler 调用 player API 后立即返回协议结果
3. 真正状态变化靠 player 回调同步到 `g_soap_runtime_state`
4. 以后做 GENA/LastChange 时，事件源也来自 player 回调

---

## 6. 状态同步建议

1. `Play/Pause/Stop/Seek` 成功后不直接“假设状态已到位”
2. 以回调事件为准更新：
- `transport_state`
- `transport_rel_time/abs_time`
- `volume/mute`
- 错误码映射

---

## 7. FFmpeg 问题结论

### 7.1 “FFmpeg 不接，是不是就没法真正播放？”

结论：**不一定**。  
关键不在“必须 FFmpeg”，而在“必须有一个真实播放后端”。

可行路径有三种：

1. 接 FFmpeg（通用软件栈）
2. 接平台原生播放器/硬件解码接口
3. 接你已有的外部播放引擎

如果三者都没有，只做当前 SOAP 状态机，那么只能“协议可通”，不能“真实播放”。

### 7.2 当前阶段建议

1. 先把 `player` 抽象接口和 mock 后端做完
2. 跑通 `SOAP -> player -> callback -> 状态同步`
3. 再接入 FFmpeg 或平台解码后端

这样风险最低，调试也最清晰。

---

## 8. 下一步落地顺序（建议）

1. 新建 `player.h/player.c` + 事件回调接口
2. 新建 `player_backend_mock.c`，先让命令与状态闭环
3. 修改 `avtransport.c/renderingcontrol.c`，改为调用 player API
4. 增加最小冒烟用例：`SetURI -> Play -> GetPositionInfo -> Seek -> Pause -> Stop`
5. 再引入真实后端（FFmpeg 或平台原生）

---

## 9. 手机控制与电视本地控制同步（含 optional）

结论：`DLNA` 不是“二选一”，手机控制和电视本地控制可以同时存在。

### 9.1 基础能力（MVP）

1. 手机（DMC）可通过 SOAP 控制电视（DMR）
2. 电视本地按键/遥控可直接调用 `player` 接口
3. 两边都作用于同一份播放状态（`player` 作为单一真实状态源）

### 9.2 同步显示能力（optional）

要让“电视本地操作后手机 UI 也及时更新”，建议实现：

1. `GENA` 订阅与 `NOTIFY`（`LastChange` 事件）
2. 事件源来自 `player` 状态回调（状态、进度、音量、静音）

如果控制端不支持订阅，可退化为轮询：

1. `GetTransportInfo`
2. `GetPositionInfo`
3. `GetVolume` / `GetMute`

### 9.3 现实兼容性说明（optional）

出现“有时能控、有时不同步”通常来自：

1. 控制端 App 对订阅/轮询支持不一致
2. 设备端 action 覆盖不完整（如未实现 `Seek`/`Mute`）
3. 系统音量与渲染器音量逻辑分离
