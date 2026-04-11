# FFmpeg 与 mpv 自编工具链教程

本文档回答三个问题：

1. 当前这台开发环境缺什么。
2. 如果要补齐 `hwdec=nvtegra` 和 `deko3d`，应该下载什么。
3. 下载后如何编译，并让 `NX-Cast` 识别到新能力。

本文档只写当前项目真正需要的结论，不写泛泛的“从源码编译媒体库”。

## 1. 先看当前环境到底缺什么

当前项目的构建探测在 [makefile](/Users/ode1l/Documents/VSCode/NX-Cast/makefile)：

```make
MPV_RENDER_DK3D_HEADER_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/mpv/render_dk3d.h" && echo 1)
FFMPEG_NVTEGRA_HEADER_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/libavutil/hwcontext_nvtegra.h" && echo 1)
MPV_EXPLICIT_NVTEGRA_HWDEC_FOUND := $(shell strings "$(PORTLIBS_PREFIX)/lib/libmpv.a" 2>/dev/null | grep -q nvtegra && echo 1)
```

当前机器实测结果：

```bash
PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch

test -f /opt/devkitpro/portlibs/switch/include/mpv/render_dk3d.h && echo yes || echo no
# no

test -f /opt/devkitpro/portlibs/switch/include/libavutil/hwcontext_nvtegra.h && echo yes || echo no
# yes

strings /opt/devkitpro/portlibs/switch/lib/libmpv.a | grep nvtegra
# 无输出
```

这说明：

1. 当前官方 `switch-libmpv` 只有 `OpenGL/libmpv render API`，没有 `render_dk3d.h`。
2. 当前 `switch-ffmpeg` 头文件里已经有 `hwcontext_nvtegra.h`，但这不等于 `libmpv` 已经能显式启用 `nvtegra`。
3. 当前这套官方环境足够跑 `ao=hos + vo=libmpv + OpenGL`，不够直接落地 `hwdec=nvtegra` 和 `deko3d`。

## 2. 先装官方基线工具链

先把官方基线装齐。它不是最终目标，但它提供：

1. `libnx`
2. `pkg-config`
3. 官方 `FFmpeg`
4. 官方 `libmpv`

命令：

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S --needed switch-dev switch-pkg-config switch-ffmpeg switch-libmpv
```

装完后，当前项目至少应该能继续走这条基线：

1. `ao=hos`
2. `vo=libmpv`
3. `OpenGL/libmpv render API`

## 3. 你真正需要下载什么

### 3.1 如果你只要当前基线

你不需要再下别的。官方 `switch-ffmpeg` 和 `switch-libmpv` 就够。

### 3.2 如果你要硬解码

你需要两样东西：

1. 一个带 `nvtegra` 支持的 `FFmpeg` fork
2. 一个能显式使用 `nvtegra` 的 `mpv/libmpv` fork

只换 `FFmpeg` 不够。因为当前项目还要依赖 `libmpv.a` 里真的出现 `nvtegra` 相关符号，`makefile` 才会把它识别成 explicit hwdec 能力。

### 3.3 如果你要 deko3d

你还需要第三样：

1. 一个带 `mpv/render_dk3d.h` 和对应 render backend 的 `mpv` fork

只要 `deko3d` 头文件或独立图形库本身没有意义。`NX-Cast` 需要的是 `mpv render API` 层真的提供 `dk3d` 接口。

## 4. 如何挑选源码仓库

这一步不要先问“谁的 fork 名字响”，先问“源码里有没有我们需要的能力”。

推荐目录：

```bash
mkdir -p ~/src/switch-media
cd ~/src/switch-media
```

### 4.1 官方上游基线

如果你只是想先跑一版上游源码：

```bash
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg-upstream
git clone https://github.com/mpv-player/mpv.git mpv-upstream
```

这只能作为“源码编译基线”，不能保证拿到 `nvtegra` 和 `render_dk3d.h`。

### 4.2 自定义 Switch fork

如果你要硬解码和 `deko3d`，把上面两个 URL 替换成你选定的 Switch fork。

下载之前，先在源码树里做这三个检查：

```bash
rg -n "hwcontext_nvtegra" ~/src/switch-media/ffmpeg-switch
rg -n "render_dk3d\\.h|deko3d|dk3d" ~/src/switch-media/mpv-switch
rg -n "nvtegra" ~/src/switch-media/mpv-switch
```

判断标准：

1. `FFmpeg` 源码里必须能找到 `hwcontext_nvtegra`。
2. `mpv` 源码里必须能找到 `render_dk3d.h` 或明确的 `dk3d/deko3d` render API 接入。
3. `mpv` 源码里必须能找到 `nvtegra`，否则即使 `FFmpeg` 有头文件，`libmpv` 也大概率还是不会显式走它。

如果这三条在源码里都找不到，这个 fork 就不是你要的。

## 5. 先准备编译环境

下面的命令假定你把库安装到当前 `dkp` 的 portlibs 目录，也就是：

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
export PKG_CONFIG_PATH="$PORTLIBS_PREFIX/lib/pkgconfig"
```

如果你不用默认路径，后面的 `--prefix` 和 `PKG_CONFIG_PATH` 要一起改。

## 6. 编译 FFmpeg

### 6.1 原则

优先使用你所选 fork 自带的 `Switch` 构建脚本或 README。

原因很简单：

1. `FFmpeg` 上游的标准 `configure` 只负责通用交叉编译。
2. `Switch + nvtegra` 这条线通常还带额外 patch。
3. 不同 fork 对 `--target-os`、线程库、平台宏和附加库的处理方式不完全一样。

### 6.2 如果 fork 没提供脚本，按这个骨架起步

```bash
cd ~/src/switch-media
mkdir -p build-ffmpeg
cd build-ffmpeg

../ffmpeg-switch/configure \
  --prefix="$PORTLIBS_PREFIX" \
  --enable-cross-compile \
  --arch=aarch64 \
  --cross-prefix=aarch64-none-elf- \
  --pkg-config=pkg-config \
  --enable-static \
  --disable-shared \
  --disable-programs \
  --disable-doc \
  --enable-pic \
  --enable-gpl \
  [这里补你的 fork README 要求的 Switch 选项]

make -j"$(sysctl -n hw.ncpu)"
make install
```

这段命令有意保留了一个占位符。原因不是文档偷懒，而是：

1. `nvtegra` 这条线不是标准上游默认能力。
2. 你最终选用的 fork 决定了最后那组 `Switch` 专用 `configure` 选项。

你真正要验收的是安装结果，不是某一串 configure 参数看起来“像是对的”。

## 7. 编译 mpv / libmpv

### 7.1 原则

`mpv` 上游现在主要用 `meson`。和 `FFmpeg` 一样，优先使用 fork 自带的 `Switch cross file`、`build.sh` 或 README。

如果你的目标包含 `deko3d`，更要优先跟 fork 自带脚本，因为这已经超出上游默认构建面。

### 7.2 最小构建目标

对 `NX-Cast` 来说，最重要的是把 `libmpv` 装进 portlibs，而不是先把桌面版播放器 `mpv` 跑起来。

推荐目标：

1. 开 `libmpv`
2. 静态安装到 `PORTLIBS_PREFIX`
3. 关闭当前项目不用的前端组件

### 7.3 如果 fork 没提供脚本，按这个骨架起步

```bash
cd ~/src/switch-media
meson setup build-mpv \
  mpv-switch \
  --prefix="$PORTLIBS_PREFIX" \
  --buildtype=release \
  --cross-file /path/to/your-switch.cross \
  -Dlibmpv=true \
  -Dcplayer=false

ninja -C build-mpv
ninja -C build-mpv install
```

这里故意没有替你虚构 `switch.cross` 的内容。原因同上：

1. 真正可用的 cross file 取决于你选的 `mpv` fork。
2. 如果这个 fork 带 `deko3d` 或 explicit `nvtegra`，它几乎一定也会带自己的平台构建假设。

## 8. 编译完成后如何验收

不要先跑项目。先验库。

### 8.1 验收 FFmpeg

```bash
test -f "$PORTLIBS_PREFIX/include/libavutil/hwcontext_nvtegra.h" && echo "ffmpeg header ok"
```

这只能证明头文件在，不证明 `mpv` 真会用。

### 8.2 验收 mpv / libmpv

```bash
test -f "$PORTLIBS_PREFIX/include/mpv/render_dk3d.h" && echo "dk3d header ok"
strings "$PORTLIBS_PREFIX/lib/libmpv.a" | rg "nvtegra"
PKG_CONFIG_PATH="$PORTLIBS_PREFIX/lib/pkgconfig" pkg-config --exists mpv && echo "pkg-config ok"
```

你至少应该得到下面的期望结果：

1. 如果你要 `deko3d`，`render_dk3d.h` 必须存在。
2. 如果你要 explicit `nvtegra`，`libmpv.a` 里必须能搜到 `nvtegra`。
3. `pkg-config --exists mpv` 必须成功，否则项目还会继续拿旧库。

## 9. 让 NX-Cast 识别到新能力

只把库装好还不够，至少要确认两层。

### 9.1 构建层

重新在项目根目录构建：

```bash
cd /Users/ode1l/Documents/VSCode/NX-Cast
make clean
make -j"$(sysctl -n hw.ncpu)"
```

`makefile` 会自动按头文件和 `libmpv.a` 内容探测能力。

### 9.2 代码层

当前代码在 [libmpv.c](/Users/ode1l/Documents/VSCode/NX-Cast/source/player/backend/libmpv.c) 里只设置了：

```c
mpv_set_option_string(g_mpv, "vo", "libmpv");
mpv_set_option_string(g_mpv, "ao", "hos");
```

这意味着：

1. 即使你已经装好了 explicit `nvtegra`，当前项目也还没有主动 `mpv_set_option_string(g_mpv, "hwdec", "nvtegra");`
2. 即使你已经装好了 `render_dk3d.h`，当前项目也还没有 `deko3d` 的 render attach 路线

所以：

1. 自编库是第一步
2. 项目接线是第二步

不要把这两件事混成一件。

## 10. 推荐的实施顺序

推荐顺序如下：

1. 先装官方 `switch-ffmpeg` 和 `switch-libmpv`，确认当前基线始终可回退。
2. 先自编 `FFmpeg`，确认 `hwcontext_nvtegra.h` 来自你自己的构建而不是旧包。
3. 再自编 `mpv/libmpv`，确认 `libmpv.a` 里真的出现 `nvtegra`。
4. 如果要 `deko3d`，最后再换成带 `render_dk3d.h` 的 `mpv` fork。
5. 最后再修改 `NX-Cast` 的 `libmpv.c` 和 render backend 接线。

这样做的好处是：

1. 每一步都能单独验收。
2. 出问题时能明确是库的问题还是项目接线的问题。

## 11. 最常见的误判

### 11.1 “有头文件就等于能用”

错。`hwcontext_nvtegra.h` 只能说明 `FFmpeg` 头文件有这套接口，不说明 `libmpv` 已经显式启用了它。

### 11.2 “能编出 libmpv 就等于有 deko3d”

错。你还需要 `mpv/render_dk3d.h` 和对应 render backend。

### 11.3 “项目 make 通过就等于硬解码已经生效”

错。当前项目代码还没有主动把 `hwdec=nvtegra` 打开。

## 12. 当前项目最现实的结论

在你现在这台机器上：

1. 官方环境足够继续推进 `ao=hos + vo=libmpv + OpenGL`
2. 如果要真正补齐 `hwdec=nvtegra`，必须换自编 `FFmpeg + mpv/libmpv`
3. 如果要真正补齐 `deko3d`，还必须换带 `render_dk3d.h` 的 `mpv` fork

所以“下载和编译”的正确策略不是盲目堆命令，而是：

1. 先找对 fork
2. 再编
3. 再验
4. 最后再改项目接线

## 13. 相关文档

1. [libmpv依赖安装.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/libmpv依赖安装.md)
2. [render设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/render设计.md)
3. [Player层设计.md](/Users/ode1l/Documents/VSCode/NX-Cast/docs/Player层设计.md)
