# Third-party code

- `firmware/mosaico` derives from ESP-Mosaico-Claw, with ESP-Claw pinned through submodules and patches. Original notices and Apache-2.0 licenses remain.
- Standalone's `components/unveil_native/lvgl` adapts the Lua/LVGL binding files from ESP-Claw `9b23c15bc28703369b4b7fad3e810f8cb535fb59`, including the Unveil native drawing changes. The Claw capability router and Mosaico display service are replaced with an application-owned runtime and raw-panel LVGL port. This is licensed code reuse, not a dependency on the Claw system runtime.
- Standalone JSON, file and delay bindings derive from those ESP-Claw modules. Worker, queue, networking and display composition are local implementations.
- `esp-mosaico-bsp` and `unveil_usb_console` retain licensed hardware-driver/console code from the pinned Mosaico source. The independent firmware does not initialize Mosaico's desktop, Works, AI runtime or Vibe Mode.
- Idea adds official ESP-Iris and retained Vibe Mode integration from the pinned esp-mosaico-utils submodule; those system dependencies belong to Idea only.
- LVGL, Lua, littlefs, ESP-IDF, TinyUSB, codec and sensor libraries retain their upstream licenses.
- Doto/Noto licenses are next to the font resources, including derived subsets.

- Unveil-Wifi.otf is derived from the existing OFL Wi-Fi font: matching Unicode coverage and advance widths, cubic outlines fitted within one original font unit and CFF subroutine compression. Reproduce with tools/compact_wifi_font.py (fontTools and cffsubr). A 24px raster sample check is recorded with hardware validation.
