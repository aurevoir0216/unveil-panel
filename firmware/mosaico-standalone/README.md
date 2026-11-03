# Standalone Unveil for Mosaico

ESP-IDF + LVGL with an application-owned Lua/worker/network/display runtime. No Mosaico-Claw desktop, Works registry, capability router, AI runtime or retained Vibe Mode runs here. Driver and UI-binding reuse is credited in the root THIRD_PARTY.md.

## Build

Use ESP-IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` and the ESP32-S31 tools:

```bash
source /path/to/esp-idf/export.sh
idf.py build
```

The component manager resolves pinned Lua/LVGL/USB dependencies. Resources contain blank defaults, the shared Lua pages and required fonts, packed as littlefs. Checked-in native vector overrides are applied during configuration.

## Installation scope

The release ZIP includes a bootloader, partition table, application, resources, offset manifest and merged factory image. **Installing it replaces the original Mosaico Flash layout and Vibe Mode.** No install or erase occurs during preparation.

Only the merged factory image is intended for address 0 during deliberate standalone installation. The separate main application is a different file. Follow the ZIP's INSTALL.txt and offset manifest; do not reuse the old Mosaico main-app address for this layout.

Wi-Fi credentials start blank and use this firmware's NVS namespace. Configure Wi-Fi and the service in Unveil's System page. USB-C uses the independent TinyUSB console. The Idea build instead gives that controller to ESP-Iris.

An orange dot-font loading screen is held until the radar frame is ready. Startup, DMA/TE feedback, hardware interfaces and performance still need physical acceptance for this new runtime.
