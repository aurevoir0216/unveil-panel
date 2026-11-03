# Mosaico distributions

All variants retain the existing Unveil UI and numeric backend protocol. Wi-Fi and service settings start blank. Newly split startup paths are build candidates, not yet hardware-accepted releases.

## Standalone

An independent ESP-IDF application owns Lua workers, bounded queues, HTTPS, files, Wi-Fi, sound, motor and a raw-panel LVGL port. DMA completion and TE events provide frame counters. It does not link or start Mosaico-Claw's desktop, Works catalog, capability router or AI services. A Lua UI does not imply use of the Claw runtime.

Its own bootloader and Flash layout replace the original retained Mosaico prefix. Installation is a deliberate device conversion; preparation does not perform it. Device NAND data is not included in a factory image.

## Desktop

Mosaico starts normally. A round desktop icon starts Unveil through the app lifecycle. A primary-page upward swipe can return to the shell; upward swipes on secondary pages stay inside Unveil. Short top-button scan dispatch is active only while Unveil owns the display; outside Unveil the original Mosaico button behavior applies. The Unveil Wi-Fi row is connection status only; use Mosaico System Settings for configuration. An app-owned orange pixel loading screen appears immediately on launch.

The app uses the paired firmware's native LVGL extensions. It is not a Works download advertised as compatible with an unmodified factory image. A separate Works package was explicitly cancelled.

## Idea

The independent UI/runtime core adds the official ESP-Iris normal-app integration. The .irisfw contains a real esp-iris-system-update/0.2 manifest, app, resources and compatible partition table. The immutable retained prefix is preserved. Its private resource image uses space released by reducing its own app slot; factory ui_apps and system ranges are untouched.

It boots as the selected native app, rather than inside Mosaico-Claw. System -> Ideas requests return to Vibe Mode. Official store publication and a six-digit number are separate steps, not implied by creating a local bundle.

## Top-button scan timing

All three variants pause the sweep while returning to the radar page. After scrolling stops, an explicitly invalidated static home frame must be submitted, transferred and presented. Only then are the sweep and start haptic released in the same UI batch. Capture requests and backend progress are not delayed or substituted. Leaving home cancels pending visual feedback.

## Acceptance boundary

Builds, dependency separation, partition fit, privacy checks and bundle inspection are recorded with prepared artifacts. Desktop local UI and display handoff have been exercised on hardware, and an earlier Idea memory fix passed startup. The latest independent/Idea paths and backend-dependent completion flow still require physical acceptance. Old tests on the dedicated framework build do not constitute acceptance of these new startup paths.
