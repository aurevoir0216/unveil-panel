# Unveil for ESP-Mosaico

此目录是 Mosaico 专用固件工程。目标为官方 ESP32-S31、16 MB Flash、480 × 480 显示屏；其他 ESP32-S3 开发板不能直接使用此固件。

## 当前下载包

- `releases/2026-11-03/unveil-mosaico.bin`：已烧录验证的主程序，5,151,072 字节。
- `app/`：配套 Lua 与字体，安装到 NAND 的 `/apps/unveil/`。
- `snapshot.json`：上游依赖提交、主固件 SHA-256 和分区信息。

主程序分区地址为 **0x210000**。当前设备沿用 Mosaico Recovery ABI 2 / layout 5，保留原有启动器、分区表、NVS、SYSTEM 和 NAND。包内不提供这些分区的设备备份，也不包含 Wi-Fi 凭据。

## 从源码构建

所有命令均在本目录执行。需要 Python 3.10+、Git 和支持 ESP32-S31 的 ESP-IDF 工具链。

精确验证过的 ESP-IDF 提交：`7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。在独立 ESP-IDF 工作副本中检出该提交，初始化子模块，执行其 `install.sh esp32s31` 并载入 `export.sh`。`tools/prepare.py` 会向这个 IDF 工作副本应用随附的兼容补丁。

```bash
source /path/to/esp-idf/export.sh
python tools/prepare.py --idf "$IDF_PATH"
python -m pip install -r requirements.txt
idf.py bmgr -c ./boards -b esp_mosaico
cp sdkconfig.release sdkconfig
idf.py build
```

`prepare.py` 初始化固定提交的上游子模块，并应用 `patches/esp-claw.patch` 和 `patches/esp-idf.patch`。已应用的补丁会跳过；不匹配的依赖或冲突会报错。

`dependencies.lock` 固定注册表组件版本。`tools/unveil_native_overrides/` 保存本版本的 LVGL / ThorVG 和屏幕传输修改，配置时由 CMake 应用。不要只拷贝 Lua 到官方未修改固件：该界面依赖新增的原生 LVGL 接口。

本仓库提供源码与精确配置；随附二进制来自已完成的原设备构建。本次仓库整理验证补丁可重建原源码与文件校验，没有重新做另一台机器上的完整交叉编译；时间戳与路径也可能使重建二进制哈希不同。

## 已有同布局 Mosaico 的升级

确认设备使用相同的 `partitions_16MB.csv` 布局，并先保留自己的备份。进入下载模式后，仅写主程序分区：

```bash
python -m esptool --chip esp32s31 --port PORT write-flash \
  0x210000 releases/2026-11-03/unveil-mosaico.bin
```

不要对本包执行整片擦除，不要把这个主程序文件写到 `0x0`，也不要直接使用默认 `idf.py flash` 覆盖保留分区。首次安装到其他布局的设备还需要匹配的启动器与恢复资源，本包不提供首次整机刷写流程。

设备启动并连网后上传配套资源：

```bash
python tools/install_app.py http://DEVICE_IP
```

安装器只更新 `scripts/`、`assets/` 与 `launcher.json`，下载读回校验每个文件。已有 `/apps/unveil/config.json` 保持原样；首次安装才使用 `config.example.json`。结束后手动重启设备加载完整版本；上传期间不要开始采集。

默认服务地址为空。安装后在 System → Server 填写自己的服务地址；仓库不附带 Wi-Fi 凭据、个人服务器地址、有效服务令牌或后端访问授权。

## 运行行为

- 主页面顺序：雷达、圆盘、图表、历史、系统。
- 圆盘保留等宽档位：轮数 1/3/5，采集 0.5/1/2/5/10 秒，分析 0/0.5/1/2/5/10 秒。分析为 0 时按原接口发出 collect 模式。
- 主页面左右切换；二级页面上滑返回。顶部实体按钮回首页并发起新扫描，扫描期间不会重复启动。
- 当次图表保持到下一次扫描或重启；历史实时向树莓派拉取。
- ESP 没有测试模式开关。测试仅由树莓派控制，使用相同请求接口。

上游工程介绍保留在 [README.upstream.md](README.upstream.md)。
