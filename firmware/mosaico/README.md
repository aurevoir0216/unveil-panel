# Unveil Desktop for Mosaico

This is the Mosaico framework version. It boots the original desktop, adds a round Unveil icon and starts the application through the existing lifecycle. It is not the independent firmware.

## Build

Use the ESP-IDF revision recorded in snapshot.json:

```bash
source /path/to/esp-idf/export.sh
python tools/prepare.py --idf "$IDF_PATH"
python -m pip install -r requirements.txt
idf.py bmgr -c ./boards -b esp_mosaico
cp sdkconfig.release sdkconfig
idf.py -B build-desktop -DESP_MOSAICO_BUILD_PROFILE=profiles/desktop.cmake build
```

UNVEIL_DISTRIBUTION defaults to desktop. The old dedicated entry remains for legacy source reproduction, but is not a new distribution choice.

## Installation

The desktop ZIP includes a normal-app .irisfw with matching app, graphics and system resources. Use the compatible Vibe Mode update workflow, then upload the NAND app:

```bash
python tools/install_app.py http://DEVICE_IP
```

The installer preserves existing preferences and read-back verifies files. Restart after all files are uploaded. Wi-Fi credentials, personal service address and device NVS are not included.

Do not treat generic native full-flash arguments as a normal app update: they contain retained base images. The prepared application bundle excludes those protected writes. Blank/incompatible devices first need the matching official Mosaico base installation.

Primary-page upward swipes return to Mosaico; secondary upward swipes return inside Unveil. Top-button scanning applies only while Unveil is active; original Mosaico behavior remains outside it.

releases/2026-11-03/unveil-mosaico.bin and snapshot.json retain the old v0.10/v0.11 dedicated-framework binary for provenance. New desktop binaries are separate multi-distribution release artifacts.
