# RFPI HTTP 接口

Unveil 是 RFPI 的设备端客户端。RFPI 负责扫描、采集、分类和历史，Unveil 按接口返回的数值绘图。仓库包含固件和协议说明。完整扫描需要运行兼容的 RFPI 后端。

## 连接

在 **系统 → 服务地址** 输入根地址，例如 `https://rfpi.example.com` 或 `http://192.0.2.10:8788`。支持域名、IPv4 和可选端口。HTTPS 使用固件内置的 CA 信任列表。设备请求 JSON，POST 使用 `Content-Type: application/json`。状态响应体上限 60,000 字节，图表和历史响应体上限 65,535 字节。

| 方法 | 路径 | 响应 |
| --- | --- | --- |
| POST | `/api/device/start` | 接受任务时返回 HTTP 202 和 `job_id` |
| GET | `/api/device/status` | 当前任务的阶段、进度与结果 |
| GET | `/api/spectrogram?job=<id>&capture=<id>` | 指定任务和可选采集片段的数值图表 |
| GET | `/api/device/history?revision=<revision>` | 历史摘要和修订号 |
| POST | `/api/device/cancel` | 取消当前任务，协议接受空 JSON 对象 |

## 开始任务

```sh
curl -X POST https://rfpi.example.com/api/device/start \
  -H 'Content-Type: application/json' \
  -d '{"mode":"scan","capture_seconds":2,"analysis_seconds":2,"discovery_min_rounds":3}'
```

```json
{"job_id":"web_example"}
```

| 字段 | 值 |
| --- | --- |
| `mode` | `scan` 采集并分析，`collect` 仅采集 |
| `capture_seconds` | 圆盘档位为 0.5、1、2、5、10 秒 |
| `analysis_seconds` | 分析档位为 0.5、1、2、5、10 秒。圆盘选 0 时发送 `mode: "collect"` 和兼容值 `analysis_seconds: 2` |
| `discovery_min_rounds` | 最少扫描轮数，取 1、3、5 |

如果后端已有任务或正在下载，RFPI 可返回 HTTP 409。参数无效时返回 HTTP 400。错误响应可带 `error` 字符串。

## 状态和进度

以下是采集中状态示例。首次连接时，设备以 `idle` 或当前服务器任务判断在线状态。开始本次任务后才展示采集进度。

```json
{
  "status": "running",
  "stage": "capturing",
  "job_id": "web_example",
  "test": false,
  "config": {"mode": "scan"},
  "total_progress": 25,
  "capture_progress": {"completed_samples": 4000000, "total_samples": 16000000},
  "candidate_processing": {"selected_windows": 2, "captured": 0, "analyzed": 0}
}
```

- `status` 使用 `idle`、`running`、`completed` 或 `failed`。运行阶段包含 `scanning`、`capturing` 和 `analyzing`。
- `total_progress` 是 0–100。缺省时设备按 `candidate_processing` 的窗口数计算。阶段进度使用 `capture_progress`、`analysis_progress` 中的样本数或处理单元数。
- 完成时，`records[].capture_id` 与 `results[].capture_id` 对应。已分析结果使用 `status: "analyzed"` 和 `prediction`（或 `raw_prediction`）中的 `CAMERA` 或 `NON_CAMERA`。显式的 `classification_validity: false` 表示该分类无效。可用 `source_job` 指定图表来源任务。
- 设备把后端进度映射到前 90%，留出最后 10% 加载和准备图表。雷达画面显示 100% 后触发结果提示。

## 图表

`job` 和 `capture` 用字母、数字、下划线或短横线。省略 `capture` 时，后端选择该任务的可用片段。找不到片段时返回 HTTP 404。

| 字段 | 数据与单位 |
| --- | --- |
| `width`、`height`、`values` | 固定 96 × 64。6,144 个按行展开的 0–63 整数。STFT 横轴为时间，频率从上到下降低 |
| `spectrum`、`spectrum_range_db` | 96 个低频到高频的整数，单位 0.1 dB。范围为两个以 dB 计的数字 |
| `activity`、`activity_range_db` | 20 个按时间排序的整数，单位 0.1 dB。功率相对该采集片段的峰值，范围以 dB 计 |
| `activity_precise`、`activity_scale`、`activity_range_precise_db` | 可选的高精度 Activity 数据。20 个整数，默认比例 100，除以比例得到 dB |
| `center_hz`、`sample_rate`、`seconds` | 中心频率 Hz、每秒样本数、片段时长秒 |
| `job`、`capture_id`、`is_current` | 图表来源和当前任务标记 |

## 历史

首次请求可传空修订号。后端按时间升序返回最多 100 条摘要，设备在本地切换排序。下一次请求附带上次的 `revision`。数据未变化时，后端返回 `unchanged: true`、原记录数和空 `records`。

```json
{
  "revision": "9a34d2f7c18b4e5f006a",
  "unchanged": false,
  "count": 1,
  "records": [
    {
      "job": "web_example",
      "chart_job": "web_example",
      "chart_capture": "capture_01",
      "state": "safe",
      "captured": 2,
      "analyzed": 2,
      "selected": 2,
      "display_time": "2026-01-01 12:00"
    }
  ]
}
```

## 测试模式

由 RFPI 切换，设备继续使用相同端点。后端从已有采集数据模拟新任务的进度，状态带 `test: true`。测试结果独立保存，下一次测试替换已结束结果，结束后 900 秒清理。历史接口只返回真实任务。 [清理策略参考](reference/unveil_test_retention.py)
