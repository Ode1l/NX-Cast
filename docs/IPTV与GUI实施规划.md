# NX-Cast IPTV 与 GUI 实施规划

更新时间：2026-06-28

## 1. 结论

可以做，但需要把目标分成两个等级：

1. IPTV MVP：难度中等
   - 读取本地或远程 M3U
   - 按分组浏览频道
   - 收藏频道
   - 直接把频道 URL 交给现有 `player/libmpv`
   - 上一台、下一台、加载和错误反馈
2. 完整 IPTV 客户端：工作量较大
   - XMLTV EPG
   - 台标下载与缓存
   - 搜索、最近播放、节目详情
   - 多播放源、自动刷新和失效恢复
   - catch-up/回看、Xtream Codes 等可选协议

播放部分不是主要难点。当前项目已经具备 `URL -> libmpv -> deko3d` 的播放链。主要工作在 GUI 基础设施、频道数据建模、网络下载、缓存、持久化和线程边界。

推荐先完成 IPTV MVP，不在第一版加入 DRM、供应商登录、转码、回看或站点专用规则。

## 2. 产品边界

### 2.1 第一版必须支持

1. 用户自带本地 M3U/M3U8 频道列表
2. 用户配置远程 M3U URL
3. `#EXTM3U` 和 `#EXTINF`
4. 常用属性：
   - `tvg-id`
   - `tvg-name`
   - `tvg-logo`
   - `group-title`
5. 常用播放选项：
   - `#EXTVLCOPT:http-user-agent`
   - `#EXTVLCOPT:http-referrer`
6. 频道分组、收藏、最近播放
7. 频道切换和失败后的明确提示
8. 继续保留 DLNA DMR，并与 IPTV 共用同一播放器会话

### 2.2 后续版本支持

1. XMLTV EPG
2. 台标下载、解码和磁盘缓存
3. 搜索和节目单筛选
4. 播放列表定时刷新
5. 多线路和失败回退
6. catch-up/回看
7. Xtream Codes API

### 2.3 明确不做

1. 仓库内置频道源或未授权内容
2. DRM 绕过
3. 广告绕过
4. 服务商私有 API 逆向
5. Switch 端转码
6. 默认关闭 TLS 校验

## 3. 当前基础与缺口

当前已有：

1. `libmpv` 播放后端
2. `ao=hos`
3. `deko3d/libmpv render API`
4. `hwdec=nvtegra` 运行时接入
5. `PlayerSnapshot` 和 `PlayerEvent`
6. 播放器 OSD、进度条、seek 和音量控制
7. mock backend
8. SD 卡运行时存储目录
9. Docker 和 GitHub Actions 构建

当前缺少：

1. 应用级页面和导航状态机
2. 可复用 GUI 控件、字体、图片和焦点系统
3. IPTV 数据模型
4. M3U 解析器
5. 通用 HTTP 客户端封装
6. 播放列表缓存和配置存储
7. 频道队列及上一台/下一台逻辑
8. EPG 和台标缓存

当前 `main.c` 直接处理日志页和播放器按键。加入 GUI 前，应先把输入分发和应用页面状态从 `main.c` 抽出。

## 4. 推荐架构

```text
main
  -> app
       -> navigation / input / task queue
       -> gui
            -> home
            -> iptv sources
            -> channel browser
            -> settings
            -> player chrome
       -> feature/iptv
            -> model
            -> m3u parser
            -> catalog
            -> favorites/history
            -> source service
            -> epg service (later)
       -> net/http_client
       -> storage/config/cache
  -> player
       -> core / backend / render / player-ui
  -> protocol/dlna
       -> renderer/player
```

关键边界：

1. IPTV 只提供媒体目录和播放请求，不进入 `player` 内部
2. IPTV 选中频道后调用 `player_set_media()` 和 `player_play()`
3. DLNA 继续直接控制同一个 player，不复制第二套播放器
4. GUI 只读取应用状态和 `PlayerSnapshot`
5. 网络和解析任务不阻塞主渲染线程
6. `main.c` 只保留初始化、主循环和退出协调

### 4.1 播放会话抢占规则

第一版采用简单、确定的 last-command-wins：

1. IPTV 正在播放时收到 DLNA `SetAVTransportURI`，DLNA 替换当前媒体
2. DLNA 正在播放时用户从 GUI 选择 IPTV 频道，IPTV 替换当前媒体
3. 不弹出确认框，避免控制端等待超时
4. app 记录 `LOCAL_IPTV` 或 `EXTERNAL_DLNA` origin，仅用于 UI 和导航，不创建第二份播放状态
5. 如果当前 URI 因外部命令改变，GUI 必须清除旧频道的“正在播放”标记
6. `PlayerSnapshot` 仍是最终运行时真相，origin 不能覆盖 player 状态

## 5. GUI 技术路线

### 5.1 推荐路线：ImGui + deko3d

播放器 OSD 继续保留当前轻量自绘实现。首页、频道列表和设置页使用 ImGui。

理由：

1. 当前项目已经有 deko3d 设备、队列、swapchain 和逐帧 present
2. `nxmp` 已证明 ImGui + deko3d + Switch 手柄输入可行
3. 频道列表、滚动、焦点、弹窗、设置和调试页面不适合继续用 5x7 字体手写
4. 可以只让 GUI 层使用 C++，player、DLNA 和 IPTV 核心继续保持 C

必须遵守：

1. GUI 不能创建第二套 `DkDevice` 或 swapchain
2. 平台渲染层统一拥有设备、队列和当前 framebuffer
3. 每帧顺序固定为：

```text
acquire image
  -> render mpv video（需要时）
  -> render player overlay
  -> render app GUI
  -> present
```

4. 引用 `nxmp` 后端代码前检查并保留许可证和来源声明

### 5.2 替代路线：Borealis

Borealis 更接近完整 Switch 应用框架，适合复杂页面、主题、动画和触摸。但它会显著扩大 C++、构建系统和渲染集成范围。第一阶段不建议同时迁移 player 和 app shell。

只有在确认产品要长期做成类似 wiliwili 的完整内容应用时，再考虑切换到 Borealis。

### 5.3 字体

IPTV 频道名通常包含中文，ImGui 默认字体不够。

推荐：

1. 使用 libnx `plGetSharedFont` 获取 Switch 系统共享字体
2. 使用 FreeType 构建 ImGui font atlas
3. 加载标准字体和简体中文字体 fallback
4. 不把字体放入 romfs

## 6. 渲染层改造

当前 `player/render/frontend.c` 以“视频前台”为中心。GUI 加入后，应改成“应用统一 compositor”。

建议先扩接口，不立即大规模移动文件：

```c
bool frontend_begin_frame(FrontendFrame *frame);
bool frontend_render_video(FrontendFrame *frame);
void frontend_render_player_overlay(FrontendFrame *frame);
void frontend_render_app_gui(FrontendFrame *frame);
void frontend_end_frame(FrontendFrame *frame);
```

等接口稳定后，再考虑把平台渲染从 `player/render` 移到 `source/platform/render`。

应用页面建议改为：

```text
APP_VIEW_HOME
APP_VIEW_IPTV_CHANNELS
APP_VIEW_IPTV_SOURCES
APP_VIEW_PLAYER
APP_VIEW_SETTINGS
APP_VIEW_LOG
```

## 7. IPTV 数据模型

建议的数据结构：

```c
typedef struct {
    char *id;
    char *name;
    char *location;        // local path or remote URL
    bool enabled;
    int refresh_minutes;
} IptvSource;

typedef struct {
    char *id;
    char *name;
    char *group;
    char *stream_url;
    char *logo_url;
    char *tvg_id;
    char *user_agent;
    char *referer;
    bool favorite;
} IptvChannel;

typedef struct {
    IptvChannel *channels;
    size_t channel_count;
    char **groups;
    size_t group_count;
    uint64_t revision;
} IptvCatalog;
```

要求：

1. 所有动态字符串都有明确所有权
2. parser 不做网络请求
3. parser 输出完整临时 catalog，成功后再原子替换当前 catalog
4. 限制文件大小、频道数量、单行长度和属性数量
5. URL 中的用户名、密码、token 和 query 不写入普通日志

## 8. M3U 解析规则

第一版解析流程：

1. 验证首个有效行是否为 `#EXTM3U`
2. 读取 `#EXTINF` 的时长、属性和逗号后的显示名称
3. 读取后续 `#EXTVLCOPT`
4. 下一个非空、非注释行作为频道 URL
5. 生成稳定 channel ID：优先 `tvg-id`，否则对 source ID、name、URL 做稳定 hash
6. 缺少 group 时归入 `Other`
7. 重复 channel ID 按确定性规则处理，并记录 WARN，不记录敏感 URL

必须区分：

1. IPTV 频道列表 M3U：包含多个频道入口
2. HLS media/master playlist：是某一个频道的播放协议

NX-Cast 只解析第一种。频道 URL 指向 HLS 时，仍直接交给 `libmpv/FFmpeg`，不重新实现 HLS。

## 9. HTTP、缓存与存储

当前开发环境已经有 Switch libcurl。建议新增 `source/net/http_client.*`，不要复用 DLNA HLS gateway 的私有下载代码。

HTTP 客户端第一版要求：

1. GET
2. HTTPS 证书校验默认开启
3. redirect 上限
4. connect/total timeout
5. 最大响应体限制
6. cancellation
7. 状态码和错误分类
8. 自定义 User-Agent，但不按平台猜测站点

建议目录：

```text
sdmc:/switch/NX-Cast/
  config/
    iptv_sources.json
    settings.json
  iptv/
    playlists/
    epg/
    logos/
    favorites.json
    history.json
  logs/
```

写文件必须使用临时文件加 rename，避免断电留下半文件。

配置建议使用一个小型、可测试的 C JSON 库。不要手写通用 JSON parser。

## 10. 线程与状态

建议线程模型：

```text
main/render thread
  -> input
  -> navigation
  -> GUI state
  -> consume worker results

worker thread
  -> download playlist/EPG/logo
  -> parse
  -> write cache
  -> push immutable result

existing player event thread
  -> libmpv events
  -> PlayerSnapshot / PlayerEvent
```

要求：

1. worker 不直接修改 GUI 容器
2. 结果通过队列回到主线程
3. 每次请求带 generation ID，过期结果丢弃
4. 页面退出或程序退出时可取消任务
5. 退出顺序保持：停止任务 -> 销毁 GUI -> 销毁 render/player -> 停 DLNA -> 停日志 -> 关网络

## 11. 页面与手柄交互

### 11.1 首页

1. DLNA 等待状态
2. IPTV
3. 设置
4. 日志/诊断

### 11.2 IPTV 频道页

1. 左侧分组
2. 中间频道列表
3. 右侧频道信息或当前节目
4. 顶部来源、刷新状态和搜索

### 11.3 默认按键

频道列表：

1. 方向键/左摇杆：移动焦点
2. `A`：播放频道
3. `B`：返回
4. `X`：收藏/取消收藏
5. `Y`：搜索或筛选
6. `L/R`：切换分组
7. `+`：退出应用

添加或编辑远程 M3U URL 时调用 libnx `swkbd` 系统软键盘，不自行实现屏幕键盘。

全屏 IPTV 播放：

1. `A`：可暂停时暂停/继续；直播不可暂停时显示控制栏
2. `B`：返回频道列表但保留当前频道状态
3. `L/R`：直播时上一台/下一台；可 seek 内容维持快退/快进
4. `X`：收藏当前频道
5. `Y`：打开频道抽屉
6. `ZL/ZR`：音量减/加
7. 左摇杆/方向键：导航频道抽屉

## 12. 分阶段实施

### P0：文档与可测试边界

状态：本规划完成后可开始。

交付：

1. 固定目录和 API 边界
2. 添加 host-test 构建入口
3. 添加脱敏的 M3U fixtures
4. 不改变真机运行行为

### P1：IPTV domain 与 M3U parser

交付：

1. `IptvSource/IptvChannel/IptvCatalog`
2. M3U parser
3. 分组、去重、稳定 ID
4. 内存清理和错误模型
5. macOS/Linux host 单元测试

验收：

1. 不需要 Switch 即可运行 parser tests
2. malformed input 不崩溃
3. ASan/UBSan 测试通过
4. 覆盖 CRLF、BOM、超长行、缺 URL、重复 ID 和 UTF-8 名称

### P2：HTTP、source store 与 cache

交付：

1. libcurl HTTP client
2. 本地文件和远程 URL source loader
3. 原子缓存
4. source 配置、收藏和历史存储
5. URL 日志脱敏

验收：

1. host fake HTTP server 测 redirect、timeout、oversize 和 4xx/5xx
2. 缓存损坏时可回退到上一次有效版本

### P3：app shell、导航与输入抽离

交付：

1. `AppState/AppView/AppAction`
2. navigation stack
3. input mapper
4. worker result queue
5. playback origin/last-command-wins 会话协调状态
6. `main.c` 只保留协调逻辑

验收：

1. 用纯状态测试验证页面切换
2. mock input 可测试焦点移动和返回行为
3. DLNA 播放行为不回归

### P4：ImGui + deko3d GUI 基础

交付：

1. ImGui context
2. Switch 手柄、触摸和剪贴输入 backend
3. deko3d renderer backend
4. Switch shared font 与中文 glyph
5. 统一 frame composer
6. 首页、设置页骨架

验收：

1. Switch 构建和链接通过
2. UI 和 mpv 使用同一 `DkDevice/queue/swapchain`
3. mock 模式可在无媒体时进入 GUI
4. 当前 player overlay 仍可显示

### P5：IPTV MVP 页面与播放

交付：

1. source 管理页
2. 分组和频道列表
3. 收藏和最近播放
4. 频道播放、上一台、下一台
5. 加载、空列表、网络错误和播放错误状态
6. 当前频道写入 player metadata
7. 使用 libnx `swkbd` 添加和编辑远程 M3U URL

验收：

1. 使用本地 fixture 可完整浏览并触发 mock player
2. 真实 player 仍使用直接 URL，不经过 HLS gateway
3. DLNA 和 IPTV 不会同时争夺两个 player 实例

### P6：真机验证与稳定性

需要 Switch 后执行：

1. 720p/1080p 布局
2. handheld/docked
3. Joy-Con、Pro Controller、触摸
4. 频道快速切换 100 次
5. Wi-Fi 中断和恢复
6. HLS、MPEG-TS、HTTP redirect、HTTPS
7. 退出时任务、curl、GUI、deko3d、mpv 和 socket 的清理顺序

### P7：EPG 与台标

交付：

1. XMLTV streaming parser
2. EPG 时间索引和时区处理
3. 当前/下一节目
4. 台标下载、解码、LRU 缓存
5. 节目详情页

## 13. 没有 Switch 时可以完成的工作

现在可以安全完成：

1. P1 全部
2. P2 的 host 实现与测试
3. P3 全部
4. P4 的编译接入和 mock 状态测试
5. P5 的页面状态、fixture 和 mock player 联调
6. Docker/GitHub Actions 构建
7. 静态分析、ASan/UBSan、内存所有权测试

现在不能可靠确认：

1. deko3d 实际画面和层叠顺序
2. Switch shared font 实际 glyph 覆盖
3. 手柄焦点手感和触摸坐标
4. 真机网络吞吐和频道切换延迟
5. `nvtegra` 是否对具体频道实际启用
6. docked/handheld 分辨率切换
7. 退出稳定性

因此无真机阶段只合并“有 host 测试或构建证据”的提交，不宣称真机功能完成。

## 14. 主要风险

1. GUI 与 mpv 争用 swapchain
   - 解决：统一 compositor，每帧只 acquire/present 一次
2. 中文字体和频道名
   - 解决：Switch shared font + FreeType，不依赖 5x7 字体
3. M3U 方言过多
   - 解决：先实现小而明确的兼容集，未知指令保留/忽略并测试
4. 远程列表包含凭据
   - 解决：日志脱敏、配置权限意识、错误页面不显示完整 URL
5. 大型列表内存压力
   - 解决：输入限制、紧凑模型、原子 catalog 替换、台标按需加载
6. GUI 框架扩大构建复杂度
   - 解决：GUI C++ 与核心 C 使用窄 C API；Docker/CI 固定依赖
7. 直播不可 seek
   - 解决：根据 `PlayerSnapshot.seekable` 动态改变按键语义和 UI

## 15. 总实施 Prompt

```text
你正在维护 Nintendo Switch homebrew 项目 NX-Cast。请先阅读 README_CN.md、ROADMAP.md、docs/Player层设计.md、docs/render设计.md、docs/IPTV与GUI实施规划.md，以及 source/player、source/protocol/dlna、source/main.c 的当前实现。

目标：在不破坏现有 DLNA DMR、libmpv、deko3d、hwdec 和播放器 OSD 的前提下，逐步加入 IPTV 与完整应用 GUI。

硬约束：
1. IPTV 频道 URL 必须继续直接交给 player/libmpv；不要恢复通用 HLS gateway，不要自己实现 HLS demux。
2. IPTV、DLNA 共用同一个 player session，不创建第二个播放器实例。
3. GUI、mpv 和 overlay 共用同一 DkDevice、queue、swapchain；每帧只 acquire/present 一次。
4. main.c 只负责初始化、主循环和退出协调；页面、输入、任务和 IPTV 逻辑放入独立模块。
5. 网络和解析任务不能阻塞渲染线程，结果通过队列回主线程。
6. 运行时资源写入 sdmc:/switch/NX-Cast；不要重新引入 romfs 依赖。
7. 不内置频道源、账号或 token，不关闭 TLS 校验，不实现 DRM/广告绕过。
8. URL、userinfo、query token 和自定义 headers 必须在日志中脱敏。
9. 保留 OpenGL fallback，但 deko3d 是发布主线。
10. 新增 C++ 只限 GUI/backend adapter，核心模型和 parser 优先保持 C，并提供窄 C API。

实施顺序严格按文档 P1 -> P2 -> P3 -> P4 -> P5；没有 Switch 时不要声称完成真机验证。

每一阶段都要：
1. 先检查 dirty worktree，不覆盖已有改动。
2. 给出小范围设计和文件列表。
3. 实现完整内存所有权和错误路径。
4. 增加 host tests、fixtures 或 mock tests。
5. 运行对应测试、git diff --check 和 Switch make 构建。
6. 更新相关文档。
7. 说明仍需真机验证的项目。

第一项任务：只实现 P1 IPTV domain 与 M3U parser，不提前接 GUI、网络或真实播放器。完成后停止并报告测试结果。
```

## 16. 分阶段 Prompts

### Prompt P1：模型和 M3U parser

```text
按 docs/IPTV与GUI实施规划.md 的 P1 实现 IPTV domain 与 M3U parser。

要求：
1. 新建 source/feature/iptv/model.*、m3u_parser.*、catalog.*，名称可按现有规范微调。
2. 支持 UTF-8、BOM、LF/CRLF、#EXTM3U、#EXTINF、tvg-id、tvg-name、tvg-logo、group-title、#EXTVLCOPT:http-user-agent、#EXTVLCOPT:http-referrer。
3. parser 只接收内存或 FILE，不做 HTTP。
4. 限制 playlist 大小、行长和频道数量。
5. 明确所有字符串所有权，失败时无泄漏且不返回半成品 catalog。
6. 为重复 ID、缺 URL、未知指令、非法属性提供确定行为。
7. 新建 host tests 和脱敏 fixtures，覆盖正常、损坏、超限、UTF-8、重复项。
8. 使用 ASan/UBSan 运行 host tests，再运行 git diff --check 和 make -j2。
9. 不改 DLNA、GUI 和 player 行为。
```

### Prompt P2：HTTP、配置与缓存

```text
按 docs/IPTV与GUI实施规划.md 的 P2 实现 IPTV source loader、HTTP client、配置和缓存。

要求：
1. 用 libcurl 建立 source/net/http_client.*，支持 HTTPS 校验、redirect、timeout、最大 body、取消和错误分类。
2. 支持本地路径和远程 URL，下载成功后调用现有 M3U parser。
3. 使用临时文件 + fsync + rename 原子写缓存。
4. 配置保存在 sdmc:/switch/NX-Cast/config 和 iptv 目录，不用 romfs。
5. URL、userinfo、query、Authorization、Cookie、Referer 在日志中脱敏。
6. worker 不直接修改 UI 或全局 catalog；返回不可变结果给主线程。
7. host fake server 测 redirect、timeout、oversize、4xx/5xx、缓存回退。
8. 更新 Docker/CI 依赖检查，但不要在 CI 运行时直接访问 devkitPro pacman 仓库。
9. 运行 host tests、git diff --check 和 make -j2。
```

### Prompt P3：app shell 与输入

```text
按 docs/IPTV与GUI实施规划.md 的 P3 抽出应用状态、页面导航、输入和后台任务结果队列。

要求：
1. 定义 AppState、AppView、AppAction、navigation stack 和 input mapper。
2. 页面至少包含 HOME、IPTV_CHANNELS、IPTV_SOURCES、PLAYER、SETTINGS、LOG。
3. main.c 只保留初始化、逐帧协调和退出，不直接处理各页面按键。
4. 当前日志滚动和 player controls 通过 action 分发保持原行为。
5. worker result 必须在主线程消费，支持 generation ID 和 cancellation。
6. 加纯状态单元测试，覆盖页面 push/pop、焦点、返回、播放态切换。
7. DLNA 启动、播放、退出顺序不得改变。
8. 运行测试、git diff --check 和 make -j2。
```

### Prompt P4：ImGui + deko3d GUI

```text
按 docs/IPTV与GUI实施规划.md 的 P4 接入 ImGui + deko3d GUI 基础。

要求：
1. 先审计 nxmp 的 ImGui Switch input/deko3d backend 和许可证，只复用许可兼容的部分并保留 notice。
2. 不创建第二套 DkDevice、queue 或 swapchain；把现有 frontend 改成统一 frame composer。
3. 帧顺序为 acquire -> mpv video -> player overlay -> ImGui -> present。
4. 加 Switch gamepad、触摸输入，支持 ImGui gamepad navigation。
5. 使用 plGetSharedFont + FreeType 构建标准和简体中文字体 atlas，不使用 romfs 字体。
6. 首页、设置页和空状态先做骨架，不提前接真实 IPTV 网络。
7. 保留当前 player overlay 和 OpenGL fallback 的编译边界。
8. 增加 mock UI state 测试，运行 git diff --check 和 make -j2。
9. 明确列出必须等真机确认的渲染、字体和输入问题。
```

### Prompt P5：IPTV MVP UI 与 player 集成

```text
按 docs/IPTV与GUI实施规划.md 的 P5 完成 IPTV MVP 页面和播放集成。

要求：
1. 实现 source 管理、分组、频道列表、收藏、最近播放和错误/空状态。
2. 本地 fixture 和远程 source 都通过同一 IptvCatalog 进入 UI。
3. 选择频道时构造 PlayerMedia，直接调用 player_set_media/player_play。
4. metadata 至少包含频道名、source ID、channel ID 和 logo URL，但不要泄漏凭据。
5. IPTV 与 DLNA 共用同一个 player；定义明确的会话抢占策略并写文档。
6. 非 seekable 直播使用 L/R 上一台/下一台；seekable 内容保持快退/快进。
7. ZL/ZR 控制音量，B 返回列表，X 收藏，Y 打开频道抽屉。
8. 用 mock player 做无 Switch 的端到端状态测试。
9. 不实现 EPG、Xtream、catch-up 或通用 HLS proxy。
10. 运行测试、git diff --check 和 make -j2。
```

### Prompt P6/P7：真机稳定性、EPG 与台标

```text
先按 docs/IPTV与GUI实施规划.md 的 P6 完成真机验证清单并修复发现的问题，再开始 P7。

P7 要求：
1. XMLTV 使用 streaming parser，避免整份 EPG 常驻多份内存。
2. 正确处理 channel mapping、UTC offset、时区和跨日节目。
3. 台标异步下载、大小限制、格式校验、LRU 内存缓存和磁盘缓存。
4. GUI 只按需上传可见台标纹理，页面退出释放 GPU 资源。
5. 不在后台线程调用 deko3d/ImGui。
6. 为 XMLTV parser、时间索引、缓存淘汰和损坏文件添加 host tests。
7. 记录性能数据：启动时间、catalog 内存、EPG 内存、频道切换延迟和帧时间。
```

## 17. 推荐下一步

没有 Switch 的情况下，下一步只执行 Prompt P1。它完全可以在 macOS 上完成和验证，并且不会改变现有真机播放、DLNA 或渲染行为。
