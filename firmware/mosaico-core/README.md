# Mosaico independent application core

Maintained LVGL/Lua runtime, board adapters and UI resources for the Vibe Mode-compatible Unveil Idea firmware. It does not start Mosaico-Claw or its desktop. The historical full-Flash project remains frozen in mosaico-standalone; changes here do not update that archived project.

The Idea adapter reuses the desktop's exact `mosaico/components/esp_lvgl_present` source and TE compose transport. Both use coherent double composition, measured panel timing, PSRAM and interrupt-driven idle touch. No second simplified flushing implementation is maintained. The application snapshot is kept in retained NVS and restored before loading Lua, so installing a clean resource image preserves device-only service settings. Chart data and history are excluded from this snapshot.
