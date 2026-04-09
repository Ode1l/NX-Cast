# Render 设计

本文档只讨论 `NX-Cast` 当前的视频显示路线、边界和后续方向。

## 1. 当前路线

当前正式渲染路线是：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

这条路线已经替代了早期“只靠 software render + framebuffer”的阶段性方案，成为当前基线。

## 2. 必须区分的两件事

### 2.1 渲染

渲染回答：

1. 视频帧如何显示到屏幕

当前路线：

1. `OpenGL/libmpv render API`

未来路线：

1. `deko3d`

### 2.2 解码

解码回答：

1. 码流如何被解出来

当前方向：

1. `hwdec=nvtegra`

所以：

- `OpenGL/deko3d` 不是硬解码
- `nvtegra` 不是渲染 API

## 3. 当前边界

当前 render 设计遵循这些边界：

1. 前台显示权由主线程/视图层持有
2. backend 不直接拥有前台 UI 生命周期
3. backend 只暴露窄 render 接口
4. render 层负责 context 生命周期与逐帧呈现

## 4. 当前模块分工

### 4.1 backend/libmpv

负责：

1. 与 `mpv render API` 对接
2. 暴露 render attach / frame render 能力

### 4.2 render/frontend

负责：

1. 平台显示接入
2. `OpenGL` context 生命周期
3. 前台视频页与日志页切换

### 4.3 render/view

负责：

1. 前台页面状态
2. 何时显示视频页
3. 何时显示日志页

## 5. 当前工具链结论

当前官方 `dkp` 工具链下：

1. `OpenGL` 路线可推进
2. `ao=hos` 可推进
3. explicit `nvtegra` hwdec backend 未真正可用
4. `mpv/render_dk3d.h` 不存在

这意味着：

1. 当前项目正式路线应继续是 `hos-audio + OpenGL`
2. `deko3d` 应保留为未来能力

## 6. deko3d 的位置

`deko3d` 不是当前默认路线，但仍然有明确意义：

1. 更接近 Switch 原生 GPU 路线
2. 更适合作为长期完整后端
3. 更适合与未来自定义媒体工具链配套

如果后续切到自定义工具链，再进入：

1. `libuam`
2. `FFmpeg(nvtegra)`
3. `mpv(deko3d + hos-audio)`

## 7. 当前后续方向

当前 render 方向不是继续换渲染 API，而是：

1. 保持 `OpenGL` 路线稳定
2. 提高 mixed transport 下的显示稳定性
3. 在工具链成熟后再考虑 `nvtegra`
4. 最后再决定是否引入 `deko3d`

## 8. 相关文档

1. [libmpv依赖安装.md](libmpv依赖安装.md)
2. [Player层设计.md](Player层设计.md)
3. [源兼容性.md](源兼容性.md)
