# Render 设计

本文档只讨论 `NX-Cast` 当前的视频显示路线、边界和后续方向。

## 1. 当前路线

当前正式渲染路线是：

1. `libmpv`
2. `ao=hos`
3. `deko3d/libmpv render API`

回退路线是：

1. `OpenGL/libmpv render API`

当前仓库默认希望在安装自定义媒体工具链时进入 `deko3d` 路径，只有在该工具链不存在时才回退到 `OpenGL`。

## 2. 必须区分的两件事

### 2.1 渲染

渲染回答：

1. 视频帧如何显示到屏幕

当前路线：

1. `deko3d/libmpv render API`

未来路线：

1. 继续加固现有 `deko3d`
2. 保留 `OpenGL` 回退

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
2. `deko3d` 设备、swapchain 与逐帧 present 生命周期
3. 必要时回退到 `OpenGL` context 生命周期
4. 前台视频页与日志页切换

### 4.3 render/view

负责：

1. 前台页面状态
2. 何时显示视频页
3. 何时显示日志页

## 5. 当前工具链结论

当前官方 `dkp` 工具链下：

1. `OpenGL` 回退路线仍可推进
2. `ao=hos` 可推进
3. explicit `nvtegra` hwdec backend 仍不应作为稳定基线

当前自定义媒体工具链下：

1. `mpv/render_dk3d.h` 存在
2. `deko3d` 路线可用
3. `hwdec=nvtegra` 才有实际验证意义

## 6. deko3d 的位置

`deko3d` 现在已经不是“未来设想”，而是当前默认发布路线：

1. 更接近 Switch 原生 GPU 路线
2. 更适合作为长期完整后端
3. 已经和当前自定义媒体工具链绑定

对应依赖：

1. `libuam`
2. `FFmpeg(nvtegra)`
3. `mpv(deko3d + hos-audio)`

## 7. 当前后续方向

当前 render 方向不是继续换渲染 API，而是：

1. 加固 `deko3d` 路线稳定性
2. 保留 `OpenGL` 回退能力
3. 提高 mixed transport 下的显示稳定性
4. 把 `nvtegra` 验证放在自定义媒体工具链上推进

## 8. 相关文档

1. [libmpv依赖安装.md](libmpv依赖安装.md)
2. [Player层设计.md](Player层设计.md)
3. [源兼容性.md](源兼容性.md)
