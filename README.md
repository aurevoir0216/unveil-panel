# Unveil

射频采集与分析的触摸界面。设备处理交互与图表绘制，树莓派提供采集、分析、进度和历史数据。

当前硬件：**ESP-Mosaico / ESP32-S31 / 480 × 480**。维护两种运行形式：

| 版本 | 运行方式 | 工程 |
| --- | --- | --- |
| Idea 独立运行版 | 保留原厂引导与 Vibe Mode，开机直接进入 Unveil，不加载 Mosaico 桌面或 Claw 框架 | [mosaico-idea](firmware/mosaico-idea/) |
| Desktop 桌面应用版 | 保留 Mosaico 桌面，通过圆形 Unveil 图标打开 | [mosaico](firmware/mosaico/) |

桌面版通过 Vibe Mode 安装固件，正常开机仍进入 Mosaico。桌面固件会自行安装配套脚本、字体和图标。六位下载编号需要在官方 Ideas 平台发布；GitHub 的安装包不提供编号。

**全盘独立版停止更新，源码保留在 [mosaico-standalone](firmware/mosaico-standalone/)，不再提供 Release 附件。** 活跃的 Idea 版使用 [mosaico-core](firmware/mosaico-core/)；后续修复不写回被冻结的全盘工程。

Idea 版复用桌面版的显示合成、刷屏同步和触摸适配器，修复了松开手指时坐标跳变造成的点按失效。真机回归覆盖设置、音量、历史排序、图表切换、顶部按钮和完整扫描。桌面版已在真机验证自动安装应用资源、保留已有设置，以及通过图标启动。

![Unveil 雷达](docs/images/radar.png)

## 下载

从 [Unveil v0.1](https://github.com/aurevoir0216/unveil/releases/tag/v0.1) 下载带 `desktop` 或 `idea` 名称的文件。

- 桌面版 ZIP 包含固件与开发用资源上传工具；新版固件在启动时自行补齐桌面应用资源，保留已有设置。
- Idea 为官方格式 `.irisfw` 安装包；提供文件不代表已上架官方 Ideas 商店或获得六位编号。

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
