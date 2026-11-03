# Unveil Idea for Mosaico

This app uses ../mosaico-core for the independent UI/runtime, adding official ESP-Iris/Vibe Mode integration. It installs as .irisfw and runs as the selected native application, rather than inside Works.

## Build

Initialize the pinned firmware/mosaico/third-party/esp-mosaico-utils submodule, install host bundle dependencies, and use the pinned ESP-IDF revision:

```bash
python -m pip install -r requirements-host.txt
source /path/to/esp-idf/export.sh
export ESP_IRIS_PYTHON="$(command -v python)"
idf.py build
cmake --build build --target system-update-bundle
```

From the repository root, `python tools/build_mosaico.py idea` builds the same bundle. Install the host requirements after initializing the toolchain submodule. `UV_SHARED_ROOT`, `UV_DESKTOP_ROOT` and `MOSAICO_UTILS_ROOT` can point to staged source trees. The archived Desktop tree provides the low-level LVGL presenter and TE transport.

The official builder writes `build/unveil_idea-system-update.irisfw`. Its private `unveil` resources start at `0x610000`, where the application slot ends. The layout preserves the factory graphics and system ranges and the retained Vibe Mode prefix.

## Install and return

Use compatible Vibe Mode/System Update tooling. Creating a package does not publish it to the store or assign a six-digit download number. System -> Ideas requests return to Vibe Mode; holding AI during power-on is the retained hardware entry.

ESP-Iris owns high-speed USB. Wi-Fi and service defaults are blank. After the first visible frame, the startup task confirms retained health. The v0.1 hardware checks cover boot, physical top-button scanning, the backend flow, UI stress and preference retention.

The device keeps its application preferences in retained NVS independently of the updatable resource image. First installation imports the existing application config when available; it does not invent a service address. Service settings, user Wi-Fi, history and NVS must never be baked into a public bundle.

## Optional store submission package

`mosaico-ideas.json`, the 64x64 RGBA icon and bilingual presentation inputs prepare the author-side store metadata. Generating the optional website/store ZIP also needs the owning tools' requirements-ideas.txt and host libraqm >= 0.9. It is separate from the already generated local .irisfw, and no store upload/publication has been performed.
