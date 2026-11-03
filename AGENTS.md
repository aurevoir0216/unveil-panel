# Project conventions

- Maintain Unveil Idea as the active Mosaico distribution. Keep the historical Desktop and full-Flash sources. Exclude both from new releases. The low-level display adapter and ESP-Iris toolchain referenced by Idea remain dependencies. A standalone Works download was explicitly cancelled.
- The Idea application uses mosaico-core and must not start or link the Mosaico-Claw application/service framework. Credit reused board drivers and low-level UI bindings.
- Preserve swipe-up Back in Idea secondary pages and the display fence that coordinates scan animation with haptic feedback.
- Idea follows the retained Vibe Mode integration contract. Never rename a raw binary or ZIP to .irisfw.
- Never package personal Wi-Fi credentials, personal server addresses, device NVS, user history or captures. Service defaults remain blank.
- Release titles use Unveil v<version>. Do not attach standalone SHA256 files. Internal verification and required bundle hashes are appropriate.
- Mark new distributions as build candidates until physical acceptance is recorded. Preparing packages does not authorize switching or erasing the device.
