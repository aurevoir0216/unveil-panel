# Mosaico distributions

Both maintained variants retain the existing Unveil UI and numeric backend protocol. The release files leave Wi-Fi and service defaults blank. Desktop starts in Mosaico; Idea starts in Unveil.

## Independent Idea application

The maintained independent runtime is in mosaico-core. The Idea project adds the official Vibe Mode/ESP-Iris installation and recovery contract. Normal boot goes directly to Unveil; Mosaico-Claw and the desktop are not linked or started. The original bootloader and Vibe Mode stay in Flash and do not run alongside the application.

The two applications share the desktop's LVGL presenter source and measured TE composition transport. Direct boot removes the shell and other applications, while preserving the display pipeline used by the desktop variant. The original simplified Idea flush implementation has been replaced. Device-only application preferences are restored from retained NVS after resource updates; chart/history data are not included in that preference snapshot.

## Frozen full-Flash project

mosaico-standalone remains in the repository without further updates. Its historical layout replaced the retained Mosaico prefix. Its binaries have been removed from Releases; archived source is not the maintained independent distribution.

## Desktop

Mosaico starts normally. A round desktop icon starts Unveil through the app lifecycle. A primary-page upward swipe can return to the shell; upward swipes on secondary pages stay inside Unveil. Short top-button scan dispatch is active only while Unveil owns the display; outside Unveil the original Mosaico button behavior applies. The Unveil Wi-Fi row is connection status only; use Mosaico System Settings for configuration. An app-owned orange pixel loading screen appears immediately on launch.

The app uses the paired firmware's native LVGL extensions. It is not a Works download advertised as compatible with an unmodified factory image. A separate Works package was explicitly cancelled.

## Idea

The maintained independent UI/runtime core adds the official ESP-Iris normal-app integration. The .irisfw contains a real esp-iris-system-update/0.2 manifest, app, resources and compatible partition table. The immutable retained prefix is preserved. Its private resource image uses space released by reducing its own app slot; factory ui_apps and system ranges are untouched.

It boots as the selected native app, rather than inside Mosaico-Claw. System -> Ideas requests return to Vibe Mode. Official store publication and a six-digit number are separate steps, not implied by creating a local bundle.

## Top-button scan timing

Both maintained variants pause the sweep while returning to the radar page. After scrolling stops, an explicitly invalidated static home frame must be submitted, transferred and presented. Only then are the sweep and start haptic released in the same UI batch. Capture requests and backend progress are not delayed or substituted. Leaving home cancels pending visual feedback.

## Acceptance boundary

The Idea path passed normal boot, the physical top button, backend progress through chart preparation and 100%, 25 primary transitions, radial limits, theme/language rebuilds and a complete resource-image update with byte-equal preferences. The Desktop firmware was installed on the same device. All 28 seeded files matched the embedded resources, existing settings were retained, and the physical icon opened Unveil with its loading frame. See [validation records](validation/) for test scope. Store publication remains separate.
