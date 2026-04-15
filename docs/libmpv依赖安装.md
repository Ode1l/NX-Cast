# libmpv 依赖安装与路线说明

这份文档只保留当前结论。

## 1. 当前推荐路线

当前仓库默认推荐的媒体工具链是 `wiliwili` 的 Switch 预编译包：

1. `libuam`
2. `switch-ffmpeg`
3. `switch-libmpv_deko3d`

原因：

1. 这条线已经提供 `deko3d + hos-audio + nvtegra` 相关能力
2. `NX-Cast` 当前代码已经接通 `render_dk3d`
3. Docker 和 GitHub Actions 也已经按这条路线安装依赖

## 2. 官方 dkp 路线的定位

官方 `dkp` 包仍然有价值，但现在只应视为回退路径：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

适合：

1. 保持最低可编译基线
2. 本地快速验证协议层和基础播放器逻辑

不适合：

1. 作为当前项目的默认发布构建环境
2. 验证 `deko3d`
3. 验证真实 `hwdec=nvtegra`

## 3. 当前项目状态

`NX-Cast` 现在已经完成：

1. `deko3d` 渲染接线
2. `show_osd` 接口与最小本地播放器控制
3. `hwdec=nvtegra` 的运行时优先设置

仍然取决于依赖包的部分：

1. `render_dk3d.h` 是否存在
2. `libmpv` 是否真是 `deko3d` 版本
3. `FFmpeg` 是否真带 `hwcontext_nvtegra`

## 4. 主文档

如果你要安装或自编 `FFmpeg/mpv`，直接看：

1. [FFmpeg与mpv自编工具链教程.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/FFmpeg与mpv自编工具链教程.md)
