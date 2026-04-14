# libmpv 依赖安装与路线说明

这份文档只保留结论，不再承载自编译细节。

## 1. 当前两条路线

### 1.1 官方基线

官方 `dkp` 路线当前稳定可用的是：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

适合：

1. 保持项目可编译
2. 做协议层和基础播放器联调

### 1.2 wiliwili-dev 路线

如果目标是：

1. `FFmpeg nvtegra`
2. `mpv hos-audio`
3. `mpv deko3d`

当前推荐直接改走 `wiliwili-dev` 的 Switch 包与构建脚本，不再自己从官方 upstream 拼参数。

## 2. 现在不再推荐的路线

下面这些做法都不再推荐：

1. 直接用 `ffmpeg-upstream`
2. 直接用 `mpv-upstream`
3. 一边查源码一边临时猜 `configure` 参数

原因是：

1. upstream 没有你要的 Switch 专用能力
2. 真正可用的 Switch 包和构建脚本已经在 `wiliwili-dev` 里现成存在

## 3. 当前主文档

如果你要开始真正自编 `FFmpeg/mpv`，直接看：

1. [FFmpeg与mpv自编工具链教程.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/FFmpeg与mpv自编工具链教程.md)

## 4. 当前项目状态

即使你把 `wiliwili-dev` 这套库装好了，`NX-Cast` 现在也还没有自动完成：

1. `deko3d` 渲染接线
2. explicit `hwdec=nvtegra` 接线

所以正确顺序是：

1. 先把库编出来
2. 再让 `NX-Cast` 成功链接
3. 最后再移植后端代码
