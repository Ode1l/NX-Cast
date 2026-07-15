# 基于 wiliwili-dev 的 Switch FFmpeg 与 libmpv 教程

这份文档替换掉之前那套“官方 upstream + 自己猜参数”以及“先折腾 SwitchWave 全量依赖”的路线。

当前对 `NX-Cast` 最实际的方案，不是自己从零拼 `FFmpeg/mpv`，而是直接复用 `wiliwili-dev` 已经验证过的 Switch 包和构建脚本。

适用目标：

1. 让 `NX-Cast` 获得能播网络流的 `FFmpeg`
2. 让 `NX-Cast` 获得 `hos-audio + deko3d` 的 `libmpv`
3. 让 Docker、本地构建和 GitHub Actions 使用同一套媒体包

## 1. 先说结论

`wiliwili-dev` 这条线分成两种：

1. 最快可用路线：直接安装它发布的预编译包
2. 需要改库时：再按它的 `scripts/switch/*/PKGBUILD` 自己重打包

如果你的目标是尽快让 `NX-Cast` 用上可工作的 Switch `FFmpeg/libmpv`，先走预编译包，不要一上来自己编。

依据：

1. [wiliwili README](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/README.md)
2. [scripts/README.md](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/README.md)
3. [scripts/build_switch.sh](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/build_switch.sh)
4. [scripts/build_switch_deko3d.sh](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/build_switch_deko3d.sh)

## 2. 为什么选 wiliwili-dev

它的方案比你前面试的那几条更适合现在的 `NX-Cast`：

1. `FFmpeg` 包已经补了 `nvtegra` 和 Switch 网络兼容 patch
   见 [switch/ffmpeg/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/ffmpeg/PKGBUILD)
2. `libmpv` 已有 `hos-audio` 版本
   见 [switch/mpv/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/mpv/PKGBUILD)
3. `libmpv` 还有 `deko3d` 版本
   见 [switch/mpv_deko3d/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/mpv_deko3d/PKGBUILD)
4. 应用层已经验证过 `render_dk3d.h` 和 `MPV_RENDER_API_TYPE_DEKO3D` 的接线方式
   见 [mpv_core.hpp](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/wiliwili/include/view/mpv_core.hpp) 和 [mpv_core.cpp](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/wiliwili/source/view/mpv_core.cpp)

## 3. 路线选择

### 3.1 OpenGL 路线

特点：

1. 最容易装起来
2. 能解决 `devkitPro` 官方 `ffmpeg/mpv` 不能播网络视频的问题
3. 不包含 `deko3d` 渲染

对应的预编译包：

1. `switch-ffmpeg-7.1-1-any.pkg.tar.zst`
2. `switch-libmpv-0.36.0-3-any.pkg.tar.zst`

来源见：
[wiliwili README](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/README.md#L280)

### 3.2 deko3d 路线

特点：

1. 更接近你现在真正想要的方向
2. 包含 `deko3d` 渲染
3. 包含 `hos-audio`
4. 更适合后续接 `hwdec`

对应的预编译包：

1. `libuam-...pkg.tar.zst`
2. `switch-ffmpeg-7.1-1-any.pkg.tar.zst`
3. `switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst`

来源见：
[build_switch_deko3d.sh](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/build_switch_deko3d.sh)

## 4. 推荐执行顺序

对 `NX-Cast` 当前阶段，建议这么走：

1. 默认安装 `wiliwili-dev` 的预编译 `deko3d` 套餐
2. 让 `NX-Cast` 直接按当前代码路径启用 `render_dk3d`
3. 只把 OpenGL 版保留为回退或最小调试环境
4. 最后才自己重新编包

不要先做的事：

1. 不要继续手搓官方 upstream `FFmpeg/mpv`
2. 不要先碰一堆 `SwitchWave` 的额外依赖
3. 不要先自己从零编 `libusbhsfs/libsmb2/libnfs`

那些适合做完整 app，不适合做你现在的最短路径。

## 5. 最快路径：直接装预编译包

### 5.1 准备环境

```bash
sudo dkp-pacman -S --needed switch-glfw switch-libwebp switch-cmake switch-curl devkitA64
```

这一步来自 [wiliwili README](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/README.md#L286)。

### 5.2 安装 OpenGL 版包

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  $base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst \
  $base_url/switch-libmpv-0.36.0-3-any.pkg.tar.zst
```

这一步装完后，你拿到的是：

1. 支持网络视频的 `FFmpeg`
2. `hos-audio` 的 `libmpv`
3. OpenGL 回退路线，不是当前仓库默认发布路线

### 5.3 安装 deko3d 版包

当前仓库默认构建和 CI 都按这套安装：

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  $base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst \
  $base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst \
  $base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst
```

这一步对应 [build_switch_deko3d.sh](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/build_switch_deko3d.sh#L10)。

注意：

1. `switch-libmpv` 和 `switch-libmpv_deko3d` 不要混装
2. 想试 `deko3d` 时，直接用 `switch-libmpv_deko3d`

## 6. 装完后如何验收

### 6.1 OpenGL 版

```bash
test -f /opt/devkitpro/portlibs/switch/include/mpv/client.h && echo "mpv headers ok"
test -f /opt/devkitpro/portlibs/switch/lib/libmpv.a && echo "libmpv ok"
pkg-config --libs mpv
```

### 6.2 deko3d 版

```bash
test -f /opt/devkitpro/portlibs/switch/include/mpv/render_dk3d.h && echo "render_dk3d ok"
test -f /opt/devkitpro/portlibs/switch/lib/libmpv.a && echo "libmpv ok"
strings /opt/devkitpro/portlibs/switch/lib/libmpv.a | rg "deko3d|hos"
```

### 6.3 FFmpeg

```bash
test -f /opt/devkitpro/portlibs/switch/include/libavutil/hwcontext_nvtegra.h && echo "nvtegra header ok"
```

如果这一条不过，说明你拿到的 `switch-ffmpeg` 不是你想要的那版。

## 7. 让 NX-Cast 直接用这套库

装完之后，直接在当前系统 `portlibs` 上编 `NX-Cast`：

```bash
cd /Users/ode1l/Documents/VSCode/NX-Cast
make -j2
```

当前项目自己的 [makefile](/Users/ode1l/Documents/VSCode/NX-Cast/makefile) 已经会探测：

1. `mpv/render_dk3d.h`
2. `libavutil/hwcontext_nvtegra.h`
3. `libmpv.a` 里是否出现 `nvtegra`

也就是说，先不需要再写额外环境变量，先看它能不能识别并自动进入 `deko3d` 路径。

## 8. Docker 与 GitHub Actions

仓库内的 `Dockerfile` 和 GitHub Actions 也默认按同一套 `wiliwili` 预编译包安装：

1. `libuam`
2. `switch-ffmpeg`
3. `switch-libmpv_deko3d`

如果你本地和 CI 的行为不一致，优先检查是否装成了 OpenGL 版 `switch-libmpv`。

## 9. 如果要自己编包，再走本地构建

只有在下面几种情况下，你才需要自己编：

1. 预编译包版本不合适
2. 你要改 `FFmpeg` patch
3. 你要改 `mpv` 的 `deko3d/hos-audio` 行为
4. 你要调试底层库 bug

这时再看：

1. [scripts/README.md](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/README.md)
2. [switch/ffmpeg/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/ffmpeg/PKGBUILD)
3. [switch/mpv/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/mpv/PKGBUILD)
4. [switch/mpv_deko3d/PKGBUILD](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/switch/mpv_deko3d/PKGBUILD)

### 8.1 本地自编的前置包

按 [scripts/README.md](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts/README.md#L10)：

```bash
sudo dkp-pacman -S --needed \
  switch-pkg-config \
  dkp-toolchain-vars \
  switch-zlib \
  switch-bzip2 \
  switch-libass \
  switch-libfribidi \
  switch-freetype \
  switch-harfbuzz \
  switch-mesa \
  switch-mbedtls
```

### 8.2 本地自编 OpenGL 版

按 `wiliwili-dev` 脚本体系，构建这些包：

1. `libuam`
2. `dav1d`
3. `libass`
4. `ffmpeg`
5. `mpv`

脚本里给的是：

```bash
cd /Users/ode1l/Documents/VSCode/others/wiliwili-dev/scripts
libs=(libuam deko3d dav1d libass ffmpeg mpv)
for lib in ${libs[@]}; do
  pushd switch/$lib
  dkp-makepkg -i
  popd
done
```

但要注意：

1. 你这份 `wiliwili-dev` 快照里 `switch/mpv_deko3d/` 只有 `PKGBUILD`，缺 `mpv.patch`
2. 所以当前这棵树本身不适合作为“直接本地自编 deko3d 包”的唯一来源

这也是为什么我仍然建议你先用预编译包。

## 10. 运行时代码接线参考

如果你后面要继续深化 `NX-Cast` 的 `deko3d` 路径，最直接的参考不是构建脚本，而是：

1. [mpv_core.hpp](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/wiliwili/include/view/mpv_core.hpp)
2. [mpv_core.cpp](/Users/ode1l/Documents/VSCode/others/wiliwili-dev/wiliwili/source/view/mpv_core.cpp)

你真正要学的是这几件事：

1. `#include <mpv/render_dk3d.h>`
2. `mpv_deko3d_init_params`
3. `MPV_RENDER_API_TYPE_DEKO3D`
4. `vo=libmpv`
5. `hwdec` 作为运行时选项设置，而不是自己写解码器

## 11. 现在最推荐你怎么做

如果你的目标是继续推进 `NX-Cast`，最稳的是：

1. 安装 `wiliwili` 的预编译 `deko3d` 套餐
2. 回到 `NX-Cast` 跑 `make -j2`
3. 看项目是否已经识别出 `render_dk3d.h` 和 `hwcontext_nvtegra.h`
4. 如果本地要复现 CI，直接用仓库 `Dockerfile`
5. 只有需要改底层库时，再进入自编包路径

这比你继续在 `SwitchWave` 里手工收 `configure` 错误更实际。

## 12. 当前结论

`wiliwili-dev` 给你的真正价值不是它的 app，而是：

1. 一套已验证可用的 Switch `FFmpeg` 包
2. 一套已验证可用的 Switch `libmpv` 包
3. 一套清楚的 `deko3d + hos-audio + nvtegra` 参考实现

对 `NX-Cast` 当前阶段，先用它的包并保持 Docker/CI 与本地一致，是最短路径。
