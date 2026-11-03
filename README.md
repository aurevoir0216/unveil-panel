# Unveil

射频采集与分析的触摸界面。设备处理交互与图表绘制，树莓派提供采集、分析、进度和历史数据。

当前硬件：**ESP-Mosaico / ESP32-S31 / 480 × 480**。维护两种运行形式：

| 版本 | 运行方式 | 工程 |
| --- | --- | --- |
| Idea 独立运行版 | 保留原厂引导与 Vibe Mode，开机直接进入 Unveil，不加载 Mosaico 桌面或 Claw 框架 | [mosaico-idea](firmware/mosaico-idea/) |
| Desktop 桌面应用版 | 保留 Mosaico 桌面，通过圆形 Unveil 图标打开 | [mosaico](firmware/mosaico/) |

Idea 是安装与分发方式。桌面固件也可打包成 Idea：Vibe Mode 更新固件后，由桌面固件自行安装配套脚本、字体和图标。六位下载编号需要在官方 Ideas 平台创建并发布应用；本地 `.irisfw` 文件本身不产生编号。

**全盘独立版停止更新，源码保留在 [mosaico-standalone](firmware/mosaico-standalone/)，不再提供 Release 附件。** 活跃的 Idea 版使用 [mosaico-core](firmware/mosaico-core/)；后续修复不写回被冻结的全盘工程。

Idea 版现复用桌面版的显示合成、刷屏同步和触摸适配器，开机直接进入 Unveil。修复松开手指时坐标跳变造成的点按失效，真机自动回归已覆盖设置、音量、历史排序和三个图表选项。此前已通过顶部按钮扫描、树莓派完整进度与图表、连续 25 次翻页、圆盘、主题／语言切换和完整资源更新后的设置保留；用户确认雷达闪烁与错位已消失。桌面版新增资源自安装仍待真机验收，发布包继续按候选管理。

![Unveil 雷达](docs/images/radar.png)

## 下载

从 [Releases](https://github.com/aurevoir0216/unveil/releases) 下载带 `desktop` 或 `idea` 名称的文件。标题使用 `Unveil v0.xx`，不单独附带 SHA256 下载文件。

- 桌面版 ZIP 包含固件与开发用资源上传工具；新版固件在启动时自行补齐桌面应用资源，保留已有设置。
- Idea 为官方格式 `.irisfw` 安装包；提供文件不代表已上架官方 Ideas 商店或获得六位编号。

旧 `v0.10 / v0.11` 是基于 Mosaico 框架的专用启动版本，不能当作完全独立固件。

## 界面与数据

保留原生 LVGL 雷达、径向等宽圆盘、三类图表、实时历史、语言、主题、静音及音量设置。图表准备计入总进度；100% 呈现结果后才播放完成声音和振动。历史排序使用居中的点阵 `NEWEST / OLDEST`，不随语言切换。

两个维护版本的顶部按钮都先回首页并停稳，再同步启动扫描动画与震动。桌面版应用内 Wi-Fi 只显示连接状态，配置使用 Mosaico 系统设置。

Wi-Fi 和服务地址默认留空，由使用者配置。发布包不包含个人 NVS、历史、采集数据或模型权重。后端协议见 [backend.md](docs/backend.md)。

## 构建与扩展

使用固定 ESP-IDF 提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，先阅读对应工程的依赖说明。

```bash
source /path/to/esp-idf/export.sh
python tools/build_mosaico.py desktop
python tools/build_mosaico.py idea
```

构建工具只编译，不自动安装或烧录。后续硬件也使用自己的目录、配置和安装契约。

独立运行核心复用了 Apache-2.0 的硬件 BSP 和部分 Lua/LVGL 绑定代码，但不链接 ESP-Claw 系统运行层。来源见 [THIRD_PARTY.md](THIRD_PARTY.md)，字体许可证随资源提供。
