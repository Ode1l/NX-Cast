# Render 设计

## 1. 目的

这份文档专门记录 player `Step 2.2` 期间讨论过的 render 技术实现路线、边界、取舍与当前落地选择。

它解决的问题不是：

1. SOAP 控制
2. source policy
3. DLNA 协议兼容性

它只回答一件事：

1. `player` 已经能控制真实 `libmpv` 之后，视频画面应该如何真正显示到 Switch 前台

当前阶段说明：

1. 本文档记录的是 `Step 2.2` 期间的路线讨论和最小落地选择。
2. 当前项目的下一阶段重点已经转向“完整播放器后端”：
   1. 真实音频输出（`ao=hos`）
   2. `OpenGL/libmpv render API` 路径
   3. `hwdec=nvtegra` 硬解码
3. 需要特别区分两件事：
   1. `deko3d` 是渲染后端
   2. `hwdec=nvtegra` 是解码后端
4. 当前已明确采用的路线是：
   1. `ao=hos`
   2. `hwdec=nvtegra`
   3. `OpenGL/libmpv render API`
5. `deko3d` 当前被降级为未来能力，原因是现有官方 `libmpv` 工具链没有提供 `mpv/render_dk3d.h`

补充说明：

1. `wiliwili-dev` 的 Switch OpenGL 版本不是“官方现成 libmpv + render_gl”的简单组合。
2. 它的实际路线是：
   1. 自编 `FFmpeg`，开启 `--enable-nvtegra`
   2. 自编 `mpv` OpenGL 版本，开启 `hos-audio`
   3. 运行时在 Switch 上请求 `hwdec=auto`
   4. 通过 `hwdec-current` 判断最终是否真的启用了硬解
3. 它同时保留独立的 `mpv_deko3d` 构建，因此 `OpenGL` 与 `deko3d` 是两条并列的渲染后端路线，而不是“OpenGL 版就没有硬解码”。
4. 这对 `NX-Cast` 的意义是：
   1. 当前阶段选择 `hos-audio + OpenGL/libmpv render API`
   2. 不等于放弃硬解码
   3. 但如果要达到 `wiliwili-dev` 那种硬解能力，最终仍需要自定义 `FFmpeg/mpv` 工具链，而不能只依赖当前官方 `dkp` 的 `libmpv`

---

## 2. 已确定边界

在进入具体路线之前，先固定几条边界。

1. 前台显示权归主线程，不归 `player backend`
2. `player backend` 继续负责播放控制、状态与事件，不直接持有平台窗口/前台 UI 生命周期
3. `player_view` 是平台显示适配层，负责前台切换、render target 生命周期与逐帧呈现
4. `main` 驱动 render loop
5. `player backend` 只能暴露很窄的 render 接口，不能把一整套平台显示职责混进 backend

这组边界是为了保证后续即使从一种 render 路线切到另一种，也不会把 player 架构重新拆掉。

---

## 3. 当前问题约束

这次讨论 `Step 2.2` 时，实际约束有这几条。

1. 项目已经有 `console` 日志前台，视频显示不能无视它
2. `player` 是 owner thread 模型，backend 与前台并不在同一线程
3. `libmpv render API` 和普通 `libmpv` 控制 API 有线程边界
4. Switch 平台长期仍可考虑 `deko3d`，但当前工具链更现实的正式路线是 `OpenGL/libmpv render API`
5. 这一步的目标是“真实视频可见”，不是一次性把最终 GPU 路线全部做完

---

## 4. 候选路线

### 4.1 路线 A：`software render + libnx framebuffer`

思路：

1. backend 维持 `vo=libmpv`
2. 使用 `libmpv render API` 的 software backend
3. `player_view` 在主线程接管 `libnx framebuffer`
4. 每帧把软件渲染结果直接写进 framebuffer

优点：

1. 实现最短，最容易先得到“真实视频可见”结果
2. 不需要先把 `EGL/GLES`、OpenGL loader、GPU context 管理一次性做完
3. 与 `console` 切换边界更清楚
4. 仍然满足“前台显示权不在 backend”的架构要求

缺点：

1. 性能上限低，分辨率与高码率压力更大
2. CPU 占用更高
3. 不代表最终正式方案
4. 后续切到 GPU 路线时仍然要再改一轮 render target 实现

适用阶段：

1. `Step 2.2` 最小接入
2. 用来验证前台切换、render loop、真实视频路径是否打通

---

### 4.2 路线 B：`EGL/GLES + libmpv OpenGL render API`

思路：

1. 主线程创建 `EGLDisplay/EGLSurface/EGLContext`
2. `player_view` 持有 OpenGL context
3. `libmpv render API` 用 OpenGL backend
4. 每帧调用 `mpv_render_context_render()` 渲染到默认 framebuffer 或自建 FBO

优点：

1. 更接近长期可维护的 GPU 路线
2. 比 software render 更有性能空间
3. 后续接更多显示层能力时路径更自然

缺点：

1. 平台初始化复杂度明显更高
2. 需要处理 `console` 与 `EGL` 的前台切换关系
3. 需要处理 GL context 生命周期、proc address、尺寸变化与渲染状态
4. 在 `Step 2.2` 阶段容易把问题一下子扩成“图形平台重构”

适用阶段：

1. 当前正式采用的 GPU 路线
2. 与 `ao=hos + hwdec=nvtegra` 组合成这一阶段的完整后端目标

参考补充：

1. `wiliwili-dev` 的 Switch OpenGL 版本就是这条大路线：
   1. 运行时使用 `MPV_RENDER_API_TYPE_OPENGL`
   2. `FFmpeg` 侧自编并开启 `--enable-nvtegra`
   3. `mpv` 侧单独维护 OpenGL 版本与 `deko3d` 版本两套构建
2. 因此 `OpenGL` 版并不自动意味着“无硬解”；真正决定硬解的是底层 `FFmpeg/mpv` 工具链与运行时 `hwdec-current`

---

### 4.3 路线 C：直接上 `deko3d`

思路：

1. 跳过 software 与 OpenGL 中间态
2. 直接设计 `deko3d` 显示路径
3. 再考虑 `libmpv` 如何接入该路径

优点：

1. 与最终 Switch 平台形态更接近
2. 未来可能更有性能和平台一致性

缺点：

1. 当时实现成本最高
2. 资料、依赖、对接边界都比前两条路线复杂
3. 在 `Step 2.2` 阶段会把“先出画面”的目标拖慢
4. 一旦设计不清楚，返工成本最大

适用阶段：

1. 未来自定义媒体工具链阶段
2. 当前不作为默认下一步

限制说明：

1. 当前官方 `libmpv` 工具链仅提供 `render.h` 与 `render_gl.h`
2. 当前缺少 `mpv/render_dk3d.h`
3. 因此当前阶段不能把 `deko3d` 当成“只差一点代码”的路径
4. 若后续切换到自定义媒体工具链，需要同时准备：
   1. `libuam`
   2. `FFmpeg --enable-nvtegra`
   3. `mpv --enable-deko3d --enable-hos-audio`
2. 在前台显示权和最小 render loop 已稳定之后再做

---

### 4.4 路线 D：让 backend 直接持有 render context

思路：

1. backend 自己创建 render context
2. backend 自己驱动显示
3. 主线程只做控制和状态展示

优点：

1. 表面上调用链短

缺点：

1. 破坏前面已经定下的 ownership 边界
2. backend 会同时负责播放、状态、平台显示、前台生命周期
3. 后续日志 UI 切换、屏幕接管、平台特定显示会变脏
4. 不利于未来 `deko3d` 路线和 UI 层演进

结论：

1. 这条路线不采用

---

## 5. 讨论时的关键分歧点

### 5.1 先出画面，还是先做最终 GPU 路线

当时存在两种不同目标：

1. 先尽快得到“真实视频可见”
2. 直接开始长期正式的 GPU render 方案

最后判断是：

1. `Step 2.2` 应该先解决“前台视频路径成立”
2. 不应该在这一阶段把 `EGL`、GL state、`deko3d` 全部一次性压进来

### 5.2 `console` 与视频前台如何共存

这里不是单纯“能不能 render”问题，而是“谁占前台”问题。

讨论后明确：

1. 日志页和视频页分开
2. 进入视频页时可以退出 `console`
3. 停止播放或离开视频页时恢复 `console`

所以 render 技术路线必须能配合这种前台切换，而不是假设日志和视频天然共存。

### 5.3 render context 什么时候创建

这次实现前讨论过两种时机：

1. 应用启动时就创建
2. 切到视频页时再创建

当时实际考虑是：

1. 如果走 OpenGL/EGL，切页时再建 context，前台切换复杂度更高
2. 如果走 software render，则可以先把 render API 连接好，前台切换只负责 framebuffer

所以最小路径更适合 software render。

### 5.4 `player_init()` 时机

还有一个隐藏问题：

1. `player` 并不是在 `main` 最开始初始化
2. 它是在 DLNA control 初始化时才真正起 backend

这意味着 render 路线不能假设“前台平台层创建时 backend 已经完全 ready”。因此 render 接口要能接受“前台先起来，backend 之后再 attach”的顺序。

---

## 6. 本次实际落地选择

这次 `Step 2.2` 最终先落了路线 A。

也就是：

1. `libmpv` 改为 `vo=libmpv`
2. backend 仅暴露：
   1. `render_supported`
   2. `render_attach_sw`
   3. `render_frame_sw`
   4. `render_detach`
3. `player_view` 在主线程负责：
   1. 接通 render API
   2. 切换前台到视频页
   3. 创建和释放 `libnx framebuffer`
   4. 每帧提交 render
4. `main` 保持：
   1. 日志页走 `consoleUpdate`
   2. 视频页走 `player_view_render_frame`

这条路径的意义不是“最终方案确定”，而是：

1. 先验证前台视频路径成立
2. 先验证 ownership 没有走歪
3. 给后续 GPU 路线留替换点

---

## 7. 当前选择的原因

本次优先选 software render 的原因是：

1. 它最符合 `Step 2.2` 的目标范围
2. 可以最小代价验证真实视频显示
3. 不会把 `player backend` 重新做成平台显示层
4. 不会过早把 `EGL/deko3d` 的复杂度带进当前阶段

一句话说：

1. 这是“先把路打通”的选择，不是“最终技术路线已定”的选择

---

## 8. 后续可演进方向

等 `Step 2.2` 跑稳之后，可以继续讨论下面几件事。

1. software render 是否只保留为 fallback
2. `Step 2.3` 是否切到 `EGL/GLES + OpenGL render API`
3. `deko3d` 是否作为最终正式路径
4. 视频页是否需要叠加调试信息和播放态 UI
5. 日志页与视频页是否需要可手动切换，而不是完全自动切换

---

## 9. 协作约定

这次实现里，技术路线讨论发生在编码过程中，说明流程上还不够稳。

后续凡是涉及下面这类工作，原则上先讨论、后实现：

1. render 路线切换
2. 平台显示 ownership 变化
3. 线程模型变化
4. backend 和 platform 层边界变化
5. 一次改动会影响后续阶段设计的问题

也就是：

1. 先把候选路线和边界写清楚
2. 再确定当前阶段要落哪一条
3. 最后再开始写代码

这样可以避免一边实现一边临时决策，降低返工成本。
