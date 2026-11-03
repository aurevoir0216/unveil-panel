# Unveil

Unveil 是射频采集与分析的设备界面。设备负责交互与可视化，树莓派负责采集、分析、图表数据和历史服务。

仓库按硬件平台组织，各平台独立构建、发布固件。当前已实现 **ESP-Mosaico / ESP32-S31，480 × 480 触摸屏**；其他平台尚未适配。

| 平台 | 状态 | 源码与安装说明 |
| --- | --- | --- |
| ESP-Mosaico | 已有真机部署与验证记录 | [firmware/mosaico](firmware/mosaico/) |

## Mosaico 当前版本

2026-11-03 快照：开机直达 Unveil、原生 LVGL 雷达与矢量圆盘、等宽档位、三类图表、实时历史、Wi-Fi、语言、主题、音量和静音设置。

- 顶部按钮返回雷达并开始新扫描；忙碌时只返回首页。
- 开始后立即显示 0%；图表准备纳入总进度，100% 显示结果后才播放完成声音和振动。
- 开始振动等待首页帧实际完成传输与呈现。
- 树莓派测试与真实历史隔离；测试结果保留 15 分钟，ESP 不生成测试采集数据。

![雷达](docs/images/radar.png)

## 目录

```text
firmware/
  mosaico/          Mosaico 独立 ESP-IDF 工程、Lua 界面、字体与固件
    app/           上传到 NAND /apps/unveil 的配套文件
    patches/       ESP-Claw 与 ESP-IDF 修改
    releases/      已验证主固件与校验值
docs/              接口、版本与验证说明
tools/             仓库快照验证工具
```

后续硬件新增到 `firmware/<platform>/`，使用自己的构建配置、适配代码和固件下载包；平台相关的屏幕、触摸、按键、声音与存储实现不混入其他平台。

## 下载与构建

从 [Releases](https://github.com/aurevoir0216/unveil/releases) 下载带 `mosaico` 名称的独立升级包，或按 [Mosaico 构建说明](firmware/mosaico/README.md) 编译。

本快照提供现有 Mosaico 分区布局的 **主程序升级包**，配套 Lua 与字体也必须安装。它不是首次烧录所需的整片 Flash 镜像，不能写到地址 0。

## 树莓派服务与数据

设备连接使用者自行部署的 RFPI 服务，默认服务地址为空，需在系统设置中填写。接口见 [docs/backend.md](docs/backend.md)。本仓库没有包含 RF 原始数据、模型权重、设备 Wi-Fi 密码、NVS、用户历史或个人设置。当前硬件测试使用已保存的真实 I/Q，不代表新发生的 RF 采集。

## 来源与验证范围

Mosaico 工程基于 [ESP-Mosaico-Claw](https://github.com/esp-mosaico/esp-mosaico-claw) 和 [ESP-Claw](https://github.com/espressif/esp-claw)，保留上游许可证；字体许可证位于 `firmware/mosaico/app/assets/`。精确依赖提交、固件哈希见 [snapshot.json](firmware/mosaico/snapshot.json)。

界面采样或电脑模拟器不能代表真机动画帧率；当前快照没有可靠的实时 CPU 占用百分比。
