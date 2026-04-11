# libmpv 依赖安装与工具链说明

本文档记录 `NX-Cast` 当前的媒体工具链结论。

## 1. 当前正式路线

当前正式路线是：

1. `libmpv`
2. `ao=hos`
3. `OpenGL/libmpv render API`

当前不再把 `deko3d` 作为立即落地目标。

## 2. 当前官方工具链结论

基于当前官方 `dkp` 路线，可以得出的结论是：

1. `ao=hos` 可用
2. `OpenGL/libmpv render API` 可用
3. `hwdec=nvtegra` 还没有在当前官方 `libmpv` 上成为真正可用的 explicit backend
4. `mpv/render_dk3d.h` 不存在，因此不构成完整 `deko3d` 路线

## 3. 当前建议安装

当前最小依赖仍然是：

1. `switch-dev`
2. `switch-pkg-config`
3. `switch-ffmpeg`
4. `switch-libmpv`

这足以支撑：

1. `libmpv` 链接
2. `hos-audio`
3. `OpenGL` 路线

## 4. 为什么还要关心自定义工具链

因为当前官方工具链并不能完整覆盖我们未来想要的路线：

1. explicit `nvtegra`
2. `deko3d`
3. `libuam`

如果未来要进入完整 Switch 媒体栈路线，就需要自定义工具链。

## 5. 参考结论

### 5.1 nxmp

`nxmp` 的结论很明确：

1. 它使用的是自编 `FFmpeg/mpv`
2. 明确依赖 `nvtegra`
3. 明确依赖 `deko3d`

### 5.2 wiliwili-dev

`wiliwili-dev` 给出的路线更接近当前项目的现实选择：

1. 自编 `FFmpeg`，开启 `--enable-nvtegra`
2. 自编 `mpv` OpenGL 版本，配合 `hos-audio`
3. 另有独立 `mpv_deko3d` 构建

这说明：

1. `OpenGL` 与硬解码并不冲突
2. 真正的限制点在工具链，不在业务代码

## 6. 当前建议

当前项目应继续：

1. 使用官方工具链推进 `hos-audio + OpenGL`
2. 把自定义工具链视为后续阶段工作
3. 不把 `deko3d` 当成当前立即前置条件

## 7. 构建

```bash
make
```

如果后续切换自定义媒体工具链，再单独维护对应脚本与安装文档。

## 8. 进一步阅读

如果你准备自己编 `FFmpeg/mpv` 来补 `nvtegra` 或 `deko3d`，直接看：

1. [FFmpeg与mpv自编工具链教程.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/FFmpeg与mpv自编工具链教程.md)
