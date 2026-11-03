# Mosaico 固件与更新

Unveil 当前维护 [Idea](../firmware/mosaico-idea/)。普通开机进入 Unveil，使用 `.irisfw` 通过 Vibe Mode 更新。引导程序和 Vibe Mode 留在设备上，可从 **系统 → Ideas** 返回。GitHub 安装包与官方 Ideas 商店上架流程分开。

## 分区与设置

Idea 使用自己的应用分区和 `unveil` 资源分区，沿用原厂保留区和显示资源范围。应用更新后从保留的 NVS 恢复已有设置。发布包内的 Wi-Fi 和服务地址留空，用户历史和采集内容由 RFPI 管理。

## 归档

| 固件 | 路径 | 历史版本 |
| --- | --- | --- |
| Mosaico 桌面版 | [`firmware/mosaico`](../firmware/mosaico/) | [v0.1](https://github.com/aurevoir0216/unveil/releases/tag/v0.1) 中的桌面 ZIP |
| 全盘独立版 | [`firmware/mosaico-standalone`](../firmware/mosaico-standalone/) | 源码归档 |

Idea 保留了经验证的底层 LVGL 显示适配器、面板同步代码和 ESP-Iris 构建依赖。相关源码目前仍位于 `firmware/mosaico` 下，由 Idea 的构建读取。
