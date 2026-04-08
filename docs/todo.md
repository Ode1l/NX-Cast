# DLNA

## 重构协议层

1. 根据官方xml模版，流式读取和发送
2. 根据macast项目，维护全部状态机和state，完全实现DLNA

## 重构player

1. 不再猜测User-Agent 完全删掉，交给ffmpeg自动处理
2. 编译硬解码+3dffmpeg/mpv
