# 通用 DLNA DMR 源适配设计

## 1. 目标

本设计文档回答一个明确问题：

1. `NX-Cast` 作为通用 `DLNA DMR`，如何尽可能兼容手机投屏给出的 URL
2. 特别是：
   1. `https mp4`
   2. `https m3u8`
   3. `bilibili` 投屏 URL

这里的目标不是把 `NX-Cast` 做成 `wiliwili` 这样的站点原生客户端，而是：

1. 在保持“通用 DMR”定位不变的前提下
2. 把“来源适配”和“播放器核心”做成正式架构
3. 避免继续按单个 bug 打补丁

---

## 2. 非目标

本设计明确不做下面两件事：

1. 不让 `SOAP` 层直接承担来源兼容逻辑
2. 不把 `bilibili` 兼容问题简单理解成“再补几个 Header”

更具体地说：

1. `NX-Cast` 不负责像 `wiliwili` 那样主动调用站点 API 获取播放地址
2. `NX-Cast` 不负责伪造完整站点登录态、签名链路和私有会话模型
3. `NX-Cast` 只处理“控制端已经给出一个 URL”之后的通用打开策略

因此本项目对 `bilibili` 的目标应定义为：

1. 尽可能兼容控制端推送过来的 `bilivideo` 直链 URL
2. 尽可能附带正确的通用访问上下文
3. 但不承诺替代官方 App 的站点鉴权能力

---

## 3. 这次对参考项目的结论

这次参考的项目：

1. `wiliwili`
2. `nxmp`
3. `pplay`

结论不是“照抄谁”，而是把它们拆成三类能力。

### 3.1 `wiliwili` 的关键价值

`wiliwili` 最值得学习的不是某个 `mpv` 参数，而是两件事：

1. 它有独立的 `mpv` 事件循环
2. 它自己拥有 `bilibili` 的来源获取能力

这意味着：

1. `START_FILE / FILE_LOADED / PLAYBACK_RESTART / END_FILE`
2. `paused-for-cache / playback-abort / seeking / core-idle`

都被它持续观测，并映射成自己的播放器事件。

更重要的是：

1. 它不是一个纯被动的 DMR
2. 它会自己去拿 `bilibili` 可播放 URL
3. 它的 HTTP 栈还持有自己的 Header / Cookie / Origin / Referer 语义

所以 `wiliwili` 能播 `bilibili`，不能被误解为：

1. 只要 generic DMR 把 `referrer` 配好
2. 就一定能稳定支持 `bilibili`

### 3.2 `nxmp` 的关键价值

`nxmp` 的价值在于：

1. 它把 `mpv` 当成通用播放核心
2. 但状态管理不写在一个同步 getter 里
3. 而是在事件循环和 UI 层持续消费 `mpv` 事件

它说明一件事：

1. generic player 可以不做站点专用逻辑
2. 但必须有真正的事件驱动状态机

### 3.3 `pplay` 的关键价值

`pplay` 更轻，但仍保留了一个关键原则：

1. `mpv_wait_event()` 需要被稳定消费

哪怕状态机比较简化，它也没有把播放器状态完全寄托在“同步查询时顺手刷新”上。

### 3.4 对 `NX-Cast` 的直接结论

当前 `NX-Cast` 和这三个项目相比，真正缺的不是单个参数，而是：

1. 没有独立 `Player Core`
2. 没有独立 `Source Strategy`
3. 没有把“通用 URL 播放”和“站点来源适配”分层

---

## 4. 当前架构问题

基于最新日志，当前问题不是单点，而是结构性问题。

### 4.1 `mpv` 事件消费模型不对

当前 `player_backend_libmpv` 主要在同步调用里刷新状态。

这会导致：

1. `mpv` 事件只有在查询或控制时才被消费
2. `FILE_LOADED / PLAYBACK_RESTART / END_FILE` 的时序会被观察动作影响
3. 计时日志和状态快照可能失真
4. `SOAP` 层会反过来影响 `player` 状态推进

这不是正式播放器核心该有的模型。

### 4.2 “来源获取”和“来源播放”混在一起

当前代码把下面两件事挤在同一层：

1. 判断 URL 属于什么来源
2. 决定如何用 `mpv` 打开它

结果就是：

1. HLS 出问题时，既可能是 TLS，也可能是状态机，也可能是 probe 策略
2. `bilibili` 出问题时，既可能是 header，也可能是 URL 已失效，也可能是站点鉴权缺失

如果不拆层，就会继续陷入：

1. 一次加 `referrer`
2. 一次改 `user-agent`
3. 一次改 `network-timeout`

但仍然无法解释“这类 URL 在架构上到底该如何处理”。

### 4.3 当前状态机还不是网络流状态机

对本项目来说，网络流播放器至少要区分：

1. `SourceAccepted`
2. `Opening`
3. `ManifestReady`
4. `Buffering`
5. `Ready`
6. `Playing`
7. `Paused`
8. `Seeking`
9. `Stopping`
10. `Ended`
11. `Error`

而不能只在：

1. `STOPPED`
2. `TRANSITIONING`
3. `PLAYING`
4. `PAUSED`

这几个外部协议状态之间摇摆。

### 4.4 `bilibili` 不属于普通 URL 兼容问题

最新日志已经说明：

1. `loadfile` 参数拼法错误不是当前根因
2. 当前失败点是真实 `HTTP 403`

这类失败的意义是：

1. URL 可能需要更强的访问上下文
2. 可能存在时效性、签名、区域、会话限制
3. 不能只用“播放器参数有 bug”来解释

因此 `bilibili` 必须被归入：

1. `vendor-sensitive source`

而不是普通 `default URL`。

---

## 5. 目标架构

建议把整体拆成四层。

### 5.1 DMR Protocol Adapter

职责：

1. 接收 `SetAVTransportURI / Play / Pause / Stop / Seek`
2. 维护 `AVTransport` 对外状态映射
3. 不直接决定 URL 兼容策略

它只做：

1. 协议解析
2. 参数校验
3. 调用 `Player Core`

### 5.2 Source Strategy Layer

这是新增的关键层。

职责：

1. 接收控制端传入的 `URI + metadata`
2. 对来源做分类
3. 构造标准化的 `ResolvedSource`
4. 决定打开策略、请求上下文和风险等级

这是 generic DMR 支持复杂来源的核心。

### 5.3 Player Core

职责：

1. 独立线程持有后端
2. 独立线程持续消费 `mpv` 事件
3. 维护真实内部状态机
4. 对外提供稳定快照和事件

`Player Core` 不关心 SOAP，也不直接理解 `bilibili` 业务。

它只关心：

1. 现在拿到的是怎样的 `ResolvedSource`
2. 应该如何打开
3. 当前真实状态是什么

### 5.4 Backend Adapter

第一版就是 `libmpv adapter`。

职责：

1. 把 `ResolvedSource` 转成 `mpv` 可接受的打开动作
2. 观察 `mpv` 属性和事件
3. 把底层状态回报给 `Player Core`

---

## 6. Source Strategy 设计

### 6.1 核心概念

新增三个核心对象。

#### `IncomingSource`

表示控制端原始输入：

1. `uri`
2. `metadata`
3. `protocol_hint`
4. `controller_hint`

#### `SourceProfile`

表示来源类型：

1. `DIRECT_HTTP_FILE`
2. `GENERIC_HLS`
3. `HEADER_SENSITIVE_HTTP`
4. `SIGNED_EPHEMERAL_URL`
5. `VENDOR_SENSITIVE_URL`

其中：

1. `bilibili` 归到 `VENDOR_SENSITIVE_URL`
2. 普通 `m3u8` 归到 `GENERIC_HLS`

#### `ResolvedSource`

这是最终交给 `Player Core` 的对象。

建议至少包含：

1. `uri`
2. `profile`
3. `headers`
4. `referer`
5. `origin`
6. `user_agent`
7. `network_timeout_ms`
8. `mpv_open_options`
9. `retry_policy`
10. `diagnostic_tags`

### 6.2 分类规则

分类不要依赖单个域名字符串，而要组合判断。

第一版可按下面规则：

1. 后缀是 `.m3u8` 或 MIME 指向 HLS
   - `GENERIC_HLS`
2. URL 是普通 `http/https` 文件型媒体
   - `DIRECT_HTTP_FILE`
3. URL 带明显签名参数
   - `SIGNED_EPHEMERAL_URL`
4. URL 命中 `bilivideo.com` 或 metadata 显示来自 bilibili 投屏
   - `VENDOR_SENSITIVE_URL`

### 6.3 Profile 决策

每类来源都要有明确策略，而不是统一塞给 `mpv`。

#### A. `DIRECT_HTTP_FILE`

策略：

1. 标准 UA
2. 默认 `network-timeout`
3. 不注入站点特定 header
4. 尽量保持最小副作用

#### B. `GENERIC_HLS`

策略：

1. 单独的 startup 策略
2. 单独的 probe 策略
3. 单独的 ready 判定
4. 单独的 retry / cache 诊断

#### C. `SIGNED_EPHEMERAL_URL`

策略：

1. 保持原 URL 完整不改写
2. 不做会破坏签名的 query 重排
3. 记录过期风险
4. 区分“来源拒绝”和“播放器失败”

#### D. `VENDOR_SENSITIVE_URL`

策略：

1. 允许注入 `Referer / Origin / User-Agent`
2. 允许按域名启用额外 header policy
3. 允许定义更保守的 retry 策略
4. 失败时优先归类为“来源访问策略失败”

### 6.4 对 `bilibili` 的专门策略

本项目作为 generic DMR，对 `bilibili` 的设计目标应该是：

1. 尽可能兼容控制端已经给出的 `bilivideo` URL
2. 给出尽可能合理的通用访问上下文
3. 在失败时，把错误准确归类

因此建议专门加一个 `BilibiliSourcePolicy`，但它不是 `bilibili client`。

它只做下面这些事：

1. 域名识别
2. 注入默认 `Referer / Origin / User-Agent`
3. 可选注入控制端传来的附加 header
4. 记录 `403 / 401 / 404 / TLS` 等来源级错误
5. 输出“当前失败属于来源访问失败，不属于播放器核心失败”

它明确不做：

1. 登录
2. Cookie 获取
3. API 签名
4. 官方播放地址获取

---

## 7. Player Core 设计

### 7.1 独立线程模型

正式模型应改成：

1. `SOAP/HTTP` 线程
2. `Player Core` 线程
3. 后端内部 `mpv` 事件循环

控制命令流程：

1. SOAP 收到命令
2. 写入 `PlayerCommandQueue`
3. `Player Core` 线程串行执行
4. `mpv` 事件被持续消费
5. 生成 `PlayerSnapshot`
6. 再由协议层读取

### 7.2 状态机

内部状态建议明确为：

1. `IDLE`
2. `OPENING`
3. `MANIFEST_READY`
4. `BUFFERING`
5. `READY`
6. `PLAYING`
7. `PAUSED`
8. `SEEKING`
9. `STOPPING`
10. `ENDED`
11. `ERROR`

其中：

1. `MANIFEST_READY`
   - 主要给 HLS 这类多阶段来源
2. `READY`
   - 表示已完成真正打开，可播放

### 7.3 对外状态映射

对外给 SOAP 时仍然可以保持 UPnP 语义：

1. `IDLE` -> `STOPPED` 或 `NO_MEDIA_PRESENT`
2. `OPENING / MANIFEST_READY / BUFFERING / SEEKING / STOPPING` -> `TRANSITIONING`
3. `READY / PLAYING` -> `PLAYING`
4. `PAUSED` -> `PAUSED_PLAYBACK`
5. `ENDED` -> `STOPPED`
6. `ERROR` -> `STOPPED` + last error

关键点在于：

1. 外部协议可以简单
2. 内部状态绝不能简单

### 7.4 快照与事件

`Player Core` 应维护一份只读快照：

1. `uri`
2. `profile`
3. `state`
4. `position_ms`
5. `duration_ms`
6. `seekable`
7. `buffering`
8. `last_error_domain`
9. `last_error_code`
10. `last_error_text`

同时发出事件：

1. `STATE_CHANGED`
2. `POSITION_CHANGED`
3. `DURATION_CHANGED`
4. `SEEKABLE_CHANGED`
5. `BUFFERING_CHANGED`
6. `SOURCE_ERROR`

---

## 8. `libmpv` Adapter 设计

### 8.1 角色

`libmpv adapter` 不再直接承担“来源判断”。

它只负责：

1. 接收 `ResolvedSource`
2. 应用运行时属性
3. 发起 `loadfile`
4. 持续消费 `mpv` 事件

### 8.2 持续观测的属性

建议固定长期观察下面这些属性：

1. `core-idle`
2. `eof-reached`
3. `pause`
4. `paused-for-cache`
5. `playback-abort`
6. `seeking`
7. `seekable`
8. `duration`
9. `playback-time`
10. `cache-speed`
11. `demuxer-cache-duration`
12. `demuxer-cache-state`
13. `file-format`
14. `current-demuxer`
15. `demuxer-via-network`
16. `stream-open-filename`
17. `stream-path`
18. `path`

### 8.3 HLS 的 ready 判定

对 `GENERIC_HLS`，不能再把“收到一个事件”当成 ready。

建议拆成三段：

1. `manifest opened`
2. `first media segment opened`
3. `first playback-ready state`

真正对外暴露 `READY/PLAYING` 时，至少应满足：

1. `FILE_LOADED` 已发生
2. 有有效 `duration` 或有效可播放流信息
3. 状态不再停留在“只有 manifest，没有实际媒体读取”

### 8.4 错误分类

`libmpv` 错误不要直接只写一行字符串。

应至少分成：

1. `OPEN_ERROR`
2. `SOURCE_ACCESS_DENIED`
3. `TLS_ERROR`
4. `DEMUX_ERROR`
5. `UNSUPPORTED_FORMAT`
6. `END_OF_STREAM`

这样上层才能正确判断：

1. 是播放器能力问题
2. 还是来源访问问题

---

## 9. 面向 `bilibili` 的 generic DMR 策略

### 9.1 设计目标

对 `bilibili` 的目标要收敛成一句话：

1. 尽可能接受控制端下发的 `bilivideo` URL
2. 但不负责替代官方 App 的来源生成能力

### 9.2 兼容动作

generic DMR 可以做的合理动作：

1. 域名识别
2. Header policy
3. Referer / Origin policy
4. URL 原样保留
5. 标准 retry / timeout policy
6. 把 `403` 明确标记为 `SOURCE_ACCESS_DENIED`

### 9.3 不要继续做的事情

不要继续把下面这些动作散落在 `libmpv.c` 的分支里：

1. 基于域名临时拼接一串 `loadfile` 参数
2. 一边调 `mpv` 参数，一边判断来源类型
3. 在播放器内部同时维护“协议语义”和“站点语义”

这些动作应移到 `Source Strategy Layer`。

### 9.4 实际结果判断

对 bilibili 这类来源，最终要允许出现三类结论：

1. `Playable`
2. `Playable with vendor headers`
3. `Not playable as generic DMR source`

这第三类结论是允许存在的。

因为 generic DMR 和 source-native client 的能力边界本来就不同。

---

## 10. 文档化的实现顺序

### Phase 1. Player Core 重构

已完成：

1. 独立 `Player Core` 线程
2. 命令队列
3. `mpv` 事件循环
4. 稳定快照

不先做这一步，后面所有来源适配都会继续被状态时序污染。

### Phase 2. Source Strategy Layer

已完成第一版：

1. `media_profile.h`
2. `source_resolver.h`
3. `policy_default.c`
4. `hls_detect.c`
5. `hls_profile.c`
6. `policy_vendor.c`

先把“URL 分类”和“打开策略”从 `player backend` 拆出去。

### Phase 3. HLS 专项

专门做：

1. manifest-ready 判定
2. segment-ready 判定
3. startup latency 观测
4. TLS / demux 错误归类

截至 `2026-04-04`，`generic/live HLS` 已经有实机通过案例，当前阶段结论变为：

1. HLS 主链不是“完全不通”
2. 当前主要矛盾已经收敛为：
   1. startup 偏慢
   2. live HLS 的 `ready` / `seekable` 分离
   3. 首段与直播窗口相关的诊断和策略调优

因此这一阶段的下一步应继续做“优化”，而不是继续停留在“能不能基本播起来”的判断。

### Phase 4. Bilibili 兼容

只做 generic DMR 范围内合理的动作：

1. header policy
2. referer/origin policy
3. 403 分类
4. 明确是否还能继续兼容

### Phase 5. 再考虑更强的站点适配

如果产品目标未来升级为：

1. 不只是 generic DMR
2. 而是“尽量像原生客户端那样支持 bilibili”

那应新增独立的 source-native adapter，而不是继续扩写 `libmpv.c`。

---

## 11. 最终设计结论

对于 `NX-Cast` 当前目标，最重要的结论是：

1. `m3u8` 和 `bilibili` 不是两个独立 bug
2. 它们共同暴露的是：
   1. `Player Core` 没独立出来
   2. `Source Strategy` 没独立出来

因此后续实现原则应固定为：

1. 先重构 `Player Core`
2. 再重构 `Source Strategy`
3. 再分别接入 `generic hls` 和 `vendor-sensitive source`

只有这样，`NX-Cast` 才是在“通用 DMR 尽可能兼容复杂来源”的方向上前进，而不是继续堆局部修补。

---

## 12. 已落地的第一阶段

截至当前这一轮，已经落地的不是完整重构，而是第一阶段骨架。

### 12.1 已新增

已新增：

1. `PlayerMedia`
2. `PlayerMediaProfile`
3. `ingress_resolve_media()`
4. `player_set_uri_with_context() -> ingress -> backend set_media`
5. `PlayerSnapshot`

对应代码：

1. [ingress.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/ingress.h)
2. [ingress.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/ingress/ingress.c)
3. [player.h](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/player.h)
4. [session.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/core/session.c)

### 12.2 当前已完成的边界

当前已经完成：

1. `SOAP` 仍然只调用 `player_set_uri()`
2. 但 `player_set_uri_with_context()` 内部已经先过 `ingress`
3. backend 不再直接接收“原始 `uri + metadata`”，而是接收 `PlayerMedia`
4. `mock` 与 `libmpv` backend 都已经接入新的 `set_media` 边界

这意味着：

1. 来源分类已经从“backend 内部临时判断”变成正式公共层
2. 后续把来源策略继续从 `libmpv backend` 拆出去时，不需要再改 SOAP 接口

### 12.3 当前还没完成

当前还没完成：

1. `policy_*` 仍是第一版策略实现，规则还不够细
2. `libmpv backend` 里仍保留一部分来源相关 runtime overrides
3. 标准化的错误域分类
4. 针对 `bilibili / HLS / signed URL` 的策略观测与回退机制

因此应把这次落地理解成：

1. `ingress -> policy -> Player Core -> Backend` 的主链已经成立
2. 后续要做的是继续把策略细节从 backend 迁出，而不是再改外围接口

### 12.4 第二阶段已落地

当前已新增并接通：

1. `session.c` 独立 owner 线程
2. 固定容量命令队列
3. `PlayerSnapshot` 快照缓存
4. `libmpv` 持续 `mpv_wait_event` 事件循环
5. `policy_default.c`
6. `hls_profile.c`
7. `policy_vendor.c`

这意味着：

1. getter 不再驱动 backend 吃事件
2. SOAP 读取的是稳定快照，而不是临时拼接状态
3. 后续 `bilibili / HLS` 的策略迁移可以继续沿 `policy_*` 与 transport policy 推进

### 12.5 后续仍需继续补完的 DMR 通用能力

这一阶段原先列出的两项待办，现在都已经从“待设计”进入“第一版已落地”：

1. `ConnectionManager/SinkProtocolInfo` 已完成第一轮扩展
2. `CurrentURIMetaData` 多路 `res/protocolInfo` 候选资源选择已落到 player `ingress`

当前的后续工作不再是“有没有这两项”，而是：

1. 继续扩完整的 `SinkProtocolInfo` 能力矩阵
2. 继续提高 `resource_select` 的评分质量
3. 结合 URL preflight、`Accept-Ranges` 和 transport policy 做更稳的资源分流

更具体的现状和边界已经并入：

1. [源兼容性.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/源兼容性.md)
2. [Player层设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/Player层设计.md)
