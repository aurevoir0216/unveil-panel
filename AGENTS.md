# Project conventions

- Maintain two active Mosaico distributions: framework desktop app firmware and the independent Unveil Idea .irisfw that retains Vibe Mode. The full-Flash project in mosaico-standalone is frozen: keep its source, do not update it and do not attach its artifacts to GitHub Releases. A standalone Works download was explicitly cancelled.
- The Idea application uses mosaico-core and must not start or link the Mosaico-Claw application/service framework. Credit reused board drivers and low-level UI bindings.
- Desktop boots Mosaico and launches Unveil from its icon. Secondary pages retain swipe-up Back; primary-page shell exit and display handoff remain available.
- Idea follows the retained Vibe Mode integration contract. Never rename a raw binary or ZIP to .irisfw.
- Never package personal Wi-Fi credentials, personal server addresses, device NVS, user history or captures. Service defaults remain blank.
- Release titles use Unveil v<version>. Do not attach standalone SHA256 files. Internal verification and required bundle hashes are appropriate.
- Mark new distributions as build candidates until physical acceptance is recorded. Preparing packages does not authorize switching or erasing the device.
