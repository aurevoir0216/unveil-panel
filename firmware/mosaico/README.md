# Unveil Desktop for Mosaico

> **Discontinued.** This directory preserves the Desktop distribution. Current releases distribute the [Idea firmware](../mosaico-idea/).

This distribution booted the Mosaico desktop and opened Unveil from its application list.

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

## Historical installation

The archived Desktop ZIP included a normal-app .irisfw with matching app, graphics and system resources. Use the compatible Vibe Mode update workflow. The normal desktop firmware installs its embedded Unveil app resources into NAND before launcher enumeration, without overwriting config.json. On normal boot, open the round Unveil icon from the Mosaico desktop.

When switching from Idea, Vibe Mode 0.2.1 may report the old app partition size during its final boot selection. If the updater confirms the new partition table was committed and the only error is validation against the old 4 MiB partition, restart once to load the committed layout. Keep the device powered while it writes.

For script-only development uploads, use:

```bash
python tools/install_app.py http://DEVICE_IP
```

The installer preserves existing preferences and read-back verifies files. Restart after all files are uploaded. Wi-Fi credentials, personal service address and device NVS are not included.

Do not treat generic native full-flash arguments as a normal app update: they contain retained base images. The prepared application bundle excludes those protected writes. Blank/incompatible devices first need the matching official Mosaico base installation.

Primary-page upward swipes return to Mosaico; secondary upward swipes return inside Unveil. Top-button scanning applies only while Unveil is active; original Mosaico behavior remains outside it.

releases/2026-11-03/unveil-mosaico.bin and snapshot.json retain the old v0.10/v0.11 dedicated-framework binary for provenance.
