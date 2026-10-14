# way2trans

[中文](README.md)

way2trans is a Linux desktop selection & screenshot translation tool built with C++20 + Qt 6: native UI, no WebView, low memory footprint.

## Features

- **Selection translation**: select text in any app, press a hotkey, get translations in a popup
- **Screenshot translation**: drag-select a screen region, OCR it, then translate
- **Silent screenshot OCR**: recognize text from a region straight into the clipboard, no window
- **Input / clipboard translation**: type in the main window, or translate clipboard content
- **Concurrent multi-engine**: Google / Bing (key-free, works out of the box), DeepL, and any OpenAI-compatible LLM (streaming supported); multiple service instances per engine type
- **Dictionary cards**: phonetics, parts of speech and multiple definitions for word lookups
- **History**: local SQLite storage with full-text search and favorites; your data never leaves the machine
- **Vocabulary book**: one-click starring, lookup-count tracking, status management, export to Anki TSV / CSV / JSON

## Platform support

| Session | Desktop | Support |
|---|---|---|
| X11 | KDE / Cinnamon / XFCE / MATE / GNOME-X11 etc. | ✅ Full |
| Wayland | KDE Plasma 5.27+ | ✅ Full |
| Wayland | wlroots (Sway / Hyprland / River …) | ✅ Full* |
| Wayland | GNOME | ❌ Not supported |

\* On wlroots, global hotkeys rely on binding commands in the compositor config. GNOME Wayland lacks the data-control and layer-shell protocols and is explicitly not supported.

## Build

Requirements: CMake ≥ 3.22, Ninja, Qt ≥ 6.4 (Core / Gui / Widgets / Network / DBus / Sql).

Optional: LayerShellQt (Wayland popup placement), libtesseract (fallback OCR), xcb-keysyms (X11 hotkeys), wl-clipboard (Wayland selection, runtime).

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev          # run tests
./build/dev/bin/way2trans   # run
```

Missing optional dependencies only trim the corresponding features; the build never hard-fails.

## Usage

```bash
way2trans trigger translate-selection   # translate current selection
way2trans trigger translate-capture     # screenshot translation
way2trans trigger capture-ocr           # silent OCR to clipboard
way2trans trigger show-main             # open main window
way2trans --text "hello"                # translate given text
```

Bind these commands to your preferred keys in your desktop / compositor hotkey settings (the standard approach under Wayland).

## OCR notes

- **WeChat OCR** (optional, best Chinese quality): at runtime, way2trans detects the OCR component of a locally installed WeChat (Linux WeChat 4.x) and invokes it in a separate helper process. **No WeChat components are distributed with this project**; the backend is hidden when not detected. For personal use only.
- **Tesseract**: fallback backend; install language packs such as `chi_sim`.

## Privacy

All history and vocabulary data stays on your machine (`~/.local/share/way2trans/`). The config file is created with 0600 permissions. A "pause recording" toggle is available in the tray menu.

## License

GPL-3.0, see [LICENSE](LICENSE).
