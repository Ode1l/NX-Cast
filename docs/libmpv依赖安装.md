# libmpv 依赖安装与验证（面向 NX-Cast）

本文档定义 `NX-Cast` 后续接入真实 `player backend` 时所需的依赖安装步骤、验证方式，以及 `libmpv` 路线与纯 `FFmpeg` 路线的区别。

当前结论先说：

1. `NX-Cast` 的正式播放后端路线定为 `libmpv`
2. `libmpv` 路线不等于“不要 FFmpeg”
3. `libmpv` 依赖底层 `FFmpeg`
4. `nxmp` 的构建线，本质上就是一条面向 Switch 的 `libmpv` 构建线

---

## 1. 路线说明

### 1.1 我们当前选哪条路线

当前正式路线：

1. `libmpv` 路线

原因：

1. `player` 层仍然可以全部保持 C
2. `libmpv` 提供的是 C API
3. 可以避免自己从零实现完整播放器
4. 更适合当前 `NX-Cast` 的工程阶段

### 1.2 `libmpv` 路线到底要装什么

这条路线应分成两档：

1. 最小安装档：直接安装 devkitPro 已提供的二进制包
   1. `switch-dev`
   2. `switch-pkg-config`
   3. `switch-ffmpeg`
   4. `switch-libmpv`
2. 高级手工构建档：只有在二进制包不可用或功能不足时，才手工编：
   1. `averne/FFmpeg`
   2. `averne/libuam`
   3. `averne/mpv`

### 1.3 如果改走纯 `FFmpeg` 路线会怎样

如果以后放弃 `libmpv`，改成自己写播放器，那么只需要：

1. 安装 `FFmpeg`
2. 不再安装 `mpv/libmpv`

但代价是你要自己实现：

1. 播放状态机
2. 音视频同步
3. seek 行为
4. 音频输出
5. 视频渲染

因此当前不建议走纯 `FFmpeg` 路线。

---

## 2. 当前应达到的最终状态

安装完成后，目标环境应满足：

1. 有可用的 `devkitPro` 和 `switch-dev`
2. 有 `PORTLIBS_PREFIX`
3. `PORTLIBS_PREFIX/include/mpv/client.h` 存在
4. `PORTLIBS_PREFIX/lib/libmpv.a` 存在
5. `PORTLIBS_PREFIX/include/libavformat` 存在
6. `PORTLIBS_PREFIX/lib/libavformat.a`、`libavcodec.a` 等存在
7. `NX-Cast` 后续可以在 `makefile` 中直接链接 `libmpv`

注意：

1. 如果 `switch-libmpv` 和 `switch-ffmpeg` 可以直接安装，就不需要先手工编 `averne/FFmpeg` 和 `averne/mpv`
2. pacman 会自动处理已声明的依赖，因此不需要你手动把所有传递依赖一个个装全

---

## 3. 第一步：补齐 devkitPro 基础环境

### 3.1 目的

先确保你当前的工具链是真正完整的 `devkitPro + switch-dev` 环境。

### 3.2 你应该先检查什么

执行：

```bash
command -v dkp-pacman || command -v pacman
test -f /opt/devkitpro/switchvars.sh && echo OK || echo MISSING
echo "$DEVKITPRO"
```

### 3.3 判断标准

如果出现以下任一情况，说明基础环境不完整：

1. 找不到 `dkp-pacman` 或 `pacman`
2. 找不到 `/opt/devkitpro/switchvars.sh`
3. `DEVKITPRO` 为空

### 3.4 官方安装方向

Unix-like 平台（Linux/macOS）应先安装 `devkitPro pacman`，再安装：

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S switch-dev
```

如果你的环境里命令名是 `pacman`，则把上面命令中的 `dkp-pacman` 换成 `pacman`。

### 3.5 重新载入环境

安装完成后执行：

```bash
source /opt/devkitpro/switchvars.sh
echo "$DEVKITPRO"
echo "$DEVKITARM"
echo "$DEVKITA64"
echo "$PORTLIBS_PREFIX"
```

### 3.6 这一阶段如何验证

执行：

```bash
which aarch64-none-elf-gcc
which aarch64-none-elf-g++
test -d "$DEVKITPRO/libnx" && echo OK || echo MISSING
test -d "$DEVKITPRO/examples/switch" && echo OK || echo MISSING
```

期望结果：

1. 能找到交叉编译器
2. `libnx` 目录存在
3. `switch` 示例目录存在

如果这一步不过，不要进入下一步。

---

## 4. 第二步：先安装最小二进制包

### 4.1 目的

先用 devkitPro 已提供的二进制包把最小可用环境装起来，而不是一开始就走 `nxmp` 那条完整手工构建线。

### 4.2 为什么我修正了之前的建议

之前那一长串包清单来自 `nxmp` 的完整应用构建需求，它覆盖了：

1. 字幕
2. 图片格式
3. 远程文件系统
4. Samba / NFS / SSH / FTP
5. 更多媒体容器与扩展功能

对 `NX-Cast` 当前目标来说，这不是最小集。

我们当前只是要：

1. 能在 Switch 上链接 `libmpv`
2. 能让 `libmpv` 背后的 FFmpeg 可用
3. 为后续真实 `player backend` 做准备

因此，第一步不应该手工装一大串库，而应该先试最小包。

### 4.3 最小建议安装包

先执行：

```bash
sudo dkp-pacman -S switch-dev switch-pkg-config switch-ffmpeg switch-libmpv
```

如果你的环境命令名是 `pacman`，则替换 `dkp-pacman`。

### 4.4 为什么这里已经包含了 `ffmpeg` 和 `mpv`

因为：

1. `switch-ffmpeg` 就是 Switch 目标平台的 FFmpeg 包
2. `switch-libmpv` 就是 Switch 目标平台的 libmpv 包

所以你刚才的质疑是对的：

1. 之前那一串包里没有直接写 `switch-ffmpeg`
2. 也没有直接写 `switch-libmpv`
3. 对我们当前目标来说，这样的安装顺序不够直接

现在应优先安装：

1. `switch-ffmpeg`
2. `switch-libmpv`

### 4.5 这一阶段如何验证

执行：

```bash
source /opt/devkitpro/switchvars.sh

test -f "$PORTLIBS_PREFIX/include/mpv/client.h" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libmpv.a" && echo OK || echo MISSING

test -d "$PORTLIBS_PREFIX/include/libavformat" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libavformat.a" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libavcodec.a" && echo OK || echo MISSING
```

期望结果：

1. `mpv/client.h` 存在
2. `libmpv.a` 存在
3. `libavformat` 头文件存在
4. `libavformat.a` / `libavcodec.a` 存在

如果这一步通过，先不要进入手工构建阶段。

### 4.6 如果找不到包怎么办

先查询：

```bash
dkp-pacman -Ss switch-libmpv
dkp-pacman -Ss switch-ffmpeg
dkp-pacman -Si switch-libmpv
dkp-pacman -Si switch-ffmpeg
```

如果这两个包能查到且能安装，就优先走二进制包路线。

只有出现以下情况，才进入后面的手工构建阶段：

1. 包不存在
2. 安装后缺少关键头文件或静态库
3. 功能不足，无法满足后续 Switch 特性需求

## 5. 第三步（仅在必要时）：手工编译 Switch 版 FFmpeg

### 5.1 目的

这是 `libmpv` 的底层依赖，也是后续硬解码路线的一部分。

### 5.2 仓库

建议使用：

```text
https://github.com/averne/FFmpeg
```

分支：

```text
nvtegra
```

### 5.3 准备源码

```bash
source /opt/devkitpro/switchvars.sh
mkdir -p ~/src
cd ~/src
git clone https://github.com/averne/FFmpeg.git ffmpeg-switch
cd ffmpeg-switch
git checkout nvtegra
```

### 5.4 配置思路

这一步的目标不是生成桌面版 FFmpeg，而是生成：

1. `aarch64-none-elf`
2. `target-os=horizon`
3. 安装到 `PORTLIBS_PREFIX`

推荐配置原则：

1. 关闭共享库
2. 打开静态库
3. 开启网络协议支持
4. 开启 `swscale / swresample`
5. 保留后续 `nvtegra` 支持

### 5.5 编译安装

执行前先清理旧配置：

```bash
make distclean 2>/dev/null || true
```

然后按 Switch 目标进行 `configure`、`make`、`make install`。

说明：

1. 具体 `configure` 选项建议以 `nxmp` 的 `BUILD.md` 为基线
2. 这一步建议单独保存为脚本，不建议每次手敲长命令

### 5.6 这一阶段如何验证

执行：

```bash
source /opt/devkitpro/switchvars.sh
test -f "$PORTLIBS_PREFIX/lib/libavformat.a" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libavcodec.a" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libswscale.a" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libswresample.a" && echo OK || echo MISSING

test -d "$PORTLIBS_PREFIX/include/libavformat" && echo OK || echo MISSING
test -d "$PORTLIBS_PREFIX/include/libavcodec" && echo OK || echo MISSING
```

期望结果：

1. 四个核心静态库都存在
2. 头文件目录存在

如果这一步不过，就不要进入 `mpv` 阶段。

---

## 6. 第四步（仅在必要时）：编译 libuam

### 6.1 目的

`libuam` 是 Switch 上与 `deko3d` 路线相关的一部分依赖，当前按 `nxmp` 路线一并准备。

### 6.2 仓库

```text
https://github.com/averne/libuam
```

### 6.3 准备源码

```bash
source /opt/devkitpro/switchvars.sh
mkdir -p ~/src
cd ~/src
git clone https://github.com/averne/libuam.git
cd libuam
```

### 6.4 构建方式

该仓库包含 `meson.build`，因此优先按 `meson` 路线构建：

```bash
meson setup build --prefix="$PORTLIBS_PREFIX"
meson compile -C build
meson install -C build
```

### 6.5 这一阶段如何验证

执行：

```bash
source /opt/devkitpro/switchvars.sh
find "$PORTLIBS_PREFIX" -iname 'uam*' | head
```

期望结果：

1. `uam` 工具或相关安装产物存在于 `PORTLIBS_PREFIX`

如果这里失败，后续 `mpv` 的某些 Switch 图形路径可能不完整。

---

## 7. 第五步（仅在必要时）：编译 Switch 版 mpv/libmpv

### 7.1 目的

这一步才是我们真正想要的目标：

1. 获取 `libmpv`
2. 供 `NX-Cast` 的 `player_backend_libmpv.c` 使用

### 7.2 仓库

```text
https://github.com/averne/mpv
```

### 7.3 准备源码

```bash
source /opt/devkitpro/switchvars.sh
mkdir -p ~/src
cd ~/src
git clone https://github.com/averne/mpv.git mpv-switch
cd mpv-switch
```

### 7.4 配置思路

这一步的关键目标不是编一个播放器程序，而是编出静态 `libmpv`。

配置原则：

1. 开启静态 `libmpv`
2. 关闭共享 `libmpv`
3. 关闭 `cplayer`
4. 保留 Switch 相关：
   1. `hos audio`
   2. `deko3d`

### 7.5 编译安装

推荐流程：

```bash
./bootstrap.py
CFLAGS="$CFLAGS -D_POSIX_VERSION=200809L -I./osdep/switch" \
TARGET=aarch64-none-elf \
./waf configure \
  --prefix="$PORTLIBS_PREFIX" \
  --enable-libmpv-static \
  --disable-libmpv-shared \
  --disable-manpage-build \
  --disable-cplayer \
  --disable-iconv \
  --disable-lua \
  --disable-sdl2 \
  --disable-gl \
  --disable-plain-gl \
  --enable-hos-audio \
  --enable-deko3d

./waf
./waf install
```

如果配置阶段生成了 `build/config.h`，可按 `nxmp` 的经验对 `HAVE_POSIX` 做一次检查和必要修正。

### 7.6 这一阶段如何验证

执行：

```bash
source /opt/devkitpro/switchvars.sh
test -f "$PORTLIBS_PREFIX/include/mpv/client.h" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libmpv.a" && echo OK || echo MISSING

aarch64-none-elf-nm -g "$PORTLIBS_PREFIX/lib/libmpv.a" | rg 'mpv_create|mpv_initialize|mpv_command'
```

期望结果：

1. `mpv/client.h` 存在
2. `libmpv.a` 存在
3. `libmpv.a` 里能找到核心符号

到这里，`libmpv` 依赖链才算真正安装完成。

---

## 8. 第六步：回到 NX-Cast 做本地集成验证

这一阶段暂时不实现完整 backend，只验证环境是否已经可链接。

### 8.1 先检查文件

执行：

```bash
source /opt/devkitpro/switchvars.sh
test -f "$PORTLIBS_PREFIX/include/mpv/client.h" && echo OK || echo MISSING
test -f "$PORTLIBS_PREFIX/lib/libmpv.a" && echo OK || echo MISSING
```

### 8.2 再检查当前项目是否仍可编

在 `NX-Cast` 仓库中执行：

```bash
make clean
make -j4
```

### 8.3 当前阶段的通过标准

在真正接入 `libmpv` 代码前，通过标准是：

1. 环境变量存在
2. `libmpv.a` 存在
3. `mpv/client.h` 存在
4. `NX-Cast` 当前工程仍可正常编译

---

## 9. 纯 FFmpeg 路线如何分叉

如果后续改成纯 `FFmpeg` 路线，则：

1. 执行到“第三步：编译 Switch 版 FFmpeg”为止
2. 不再执行：
   1. `libuam`
   2. `mpv/libmpv`

然后在 `NX-Cast` 里直接实现：

1. `player_backend_ffmpeg.c`
2. 自己的音频输出
3. 自己的视频渲染
4. 自己的状态机与同步

这条路线工作量更大，因此当前不建议采用。

---

## 10. 建议的实际执行顺序

推荐你严格按这个顺序来，不要跳步：

1. 修好 `devkitPro pacman` 基础环境
2. 安装 `switch-dev switch-pkg-config switch-ffmpeg switch-libmpv`
3. 先验证 `mpv/client.h`、`libmpv.a`、`libavformat.a` 是否已存在
4. 如果最小二进制包路线通过，就直接进入 `NX-Cast` 集成
5. 只有最小路线失败或功能不足时，再进入：
   1. `averne/FFmpeg`
   2. `averne/libuam`
   3. `averne/mpv`

---

## 11. 当前项目状态备注

当前 `NX-Cast` 仓库中已经完成：

1. `player` 外层保持 C
2. `player` 已经支持 backend 选择
3. 已有 `player_backend_libmpv.c` 占位
4. 当前更适合先验证二进制包路线，而不是立即手工重建整条媒体栈

当前还没完成：

1. 真实 `libmpv` 链接
2. 真实 URL 播放

所以本阶段文档的目标不是“马上播放”，而是先把依赖链装对。

---

## 12. 参考来源

1. Switchbrew 开发环境说明  
   https://switchbrew.org/wiki/Setting_up_Development_Environment
2. devkitPro pacman 说明  
   https://devkitpro.org/wiki/devkitPro_pacman
3. `nxmp` 本地构建说明  
   [BUILD.md](/Users/ode1l/Documents/VSCode/others/nxmp-master/BUILD.md)
4. `averne/mpv`  
   https://github.com/averne/mpv
5. `averne/FFmpeg`  
   https://github.com/averne/FFmpeg/tree/nvtegra
6. `averne/libuam`  
   https://github.com/averne/libuam
