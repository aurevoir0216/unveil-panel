# RFPI 接口约定

Unveil 在设备端渲染界面；树莓派负责扫描、分析、数值图表与历史。设备默认使用 HTTPS，地址可由设置修改。主固件仓库不是完整 RFPI 后端安装包。

| 请求 | 用途 |
| --- | --- |
| `POST /api/device/start` | 开始一次采集/分析 |
| `GET /api/device/status` | 获取阶段、样本/处理进度和结果 |
| `GET /api/spectrogram?job=...&capture=...` | 获取 96 × 64 图表数值及 spectrum/activity 数组 |
| `GET /api/device/history?revision=...` | 拉取真实历史与修订号 |
| `POST /api/device/cancel` | 协议保留的取消操作 |

开始请求保留现有 JSON 字段：`mode`、`capture_seconds`、`analysis_seconds`、`discovery_min_rounds`。分析窗口选择 0 时，`mode` 为 `collect`，接口中的 `analysis_seconds` 仍按原格式传 2；其他值使用 `scan`。

图表由 ESP 原生绘制，不从服务器下载一张已绘制的整页界面。Spectrum 表示频谱；STFT 表示时频强度；Activity 表示随时间变化的相对功率。Capture/Analysis 进度来自后端，图表准备占总进度的最后部分，结果反馈等待总进度 100%。

## 树莓派测试策略

测试开关只存在于树莓派侧。设备仍通过上述接口主动发起请求，树莓派随机选择已有 I/Q，模拟采集阶段并进行分析，返回同样的数据格式。已有 I/Q 来源真实，但一次测试不等于一次新的 RF 采集。

测试结果放在独立的 `unveil_test/run_*`，不合并进真实历史；开始下一次测试替换上次已结束结果，结束后 900 秒清除。活动测试和真实 jobs 目录受保护。`reference/unveil_test_retention.py` 保留本版本清理策略源码供接口维护参考，它不是可单独部署的完整服务。
