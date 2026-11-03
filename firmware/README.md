# Mosaico firmware

Two maintained distributions retain the original bootloader and Vibe Mode:
- `mosaico-idea/`: boot directly into the independent Unveil runtime.
- `mosaico/`: original Mosaico desktop with a circular Unveil app icon.

`mosaico-core/` owns the maintained independent runtime, board adapter and UI resources. Idea is a distribution mechanism; the desktop firmware can also be submitted as an Idea when its app resources install automatically.

`mosaico-standalone/` is the frozen historical full-Flash project. Keep its source unchanged; do not build new releases or attach its binaries to GitHub Releases. Works downloads remain cancelled.
