cmake_minimum_required(VERSION 3.22)

project(way2trans
    VERSION 0.1.0
    DESCRIPTION "Linux word/screenshot translation tool"
    LANGUAGES CXX)

# --- Global build settings -------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json for clangd / clang-format tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All binaries land in <build>/bin so `./build/dev/bin/way2trans` works (AGENTS.md).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Qt automoc/autouic/autorcc for the whole tree.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# The application id / D-Bus service name, single source of truth (AGENTS.md).
set(WAY2TRANS_APP_ID "io.github.way2trans")

# --- Required dependencies -------------------------------------------------
# Qt 6.4+ with the modules listed in AGENTS.md.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network DBus Sql)

# Qt Test is required for the test target but optional for a plain build.
find_package(Qt6 6.4 QUIET COMPONENTS Test)

# Qt Linguist tools drive i18n. Optional: without them the app still builds and
# runs with the English source strings (DESIGN.md §7 i18n policy). Translations
# are hand-maintained per feature; other languages are added in M5.
find_package(Qt6 6.4 QUIET COMPONENTS LinguistTools)
set(WAY2TRANS_HAVE_TRANSLATIONS ${Qt6LinguistTools_FOUND})
set(WAY2TRANS_TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/translations/way2trans_zh_CN.ts)
if(WAY2TRANS_HAVE_TRANSLATIONS)
    # Developer-only: refresh the .ts from all sources. Not part of ALL, so it
    # never clobbers hand-written translations during a normal build.
    add_custom_target(update_translations
        COMMAND Qt6::lupdate -recursive ${CMAKE_CURRENT_SOURCE_DIR}/src
                -ts ${WAY2TRANS_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating translation source files from tr() strings"
        VERBATIM)
endif()

# --- Optional dependencies (feature-gated, never hard-fail) ----------------
# Platform backends (M1+) will grow here; probing now keeps the feature summary
# honest and lets a minimal environment still build the X11-capable core.
find_package(LayerShellQt QUIET)   # Wayland popup/overlay placement (M2); target LayerShellQt::Interface
find_package(PkgConfig QUIET)

set(WAY2TRANS_HAVE_LAYERSHELL ${LayerShellQt_FOUND})

# X11 global hotkeys (XGrabKey) need xcb + xcb-keysyms. Both are optional: when
# xcb-keysyms is absent the X11HotkeyBackend is not compiled, the build still
# succeeds, and the app falls back to the Null hotkey backend at runtime
# (DESIGN.md §4.2, §11). Probed here so the feature summary stays honest and the
# gate is visible to every subdirectory.
set(WAY2TRANS_HAVE_XCB_KEYSYMS OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB IMPORTED_TARGET QUIET xcb)
    pkg_check_modules(XCB_KEYSYMS IMPORTED_TARGET QUIET xcb-keysyms)
    if(XCB_FOUND AND XCB_KEYSYMS_FOUND)
        set(WAY2TRANS_HAVE_XCB_KEYSYMS ON)
    endif()
endif()

# Tesseract OCR backend (DESIGN.md §6). Needs libtesseract + leptonica via
# pkg-config. Optional: when absent the TesseractOcr backend is not compiled,
# the build still succeeds, and OcrService reports OCR unavailable at runtime
# (screenshot translation then degrades with a hint). Never a hard failure.
set(WAY2TRANS_HAVE_TESSERACT OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT IMPORTED_TARGET QUIET tesseract)
    pkg_check_modules(LEPTONICA IMPORTED_TARGET QUIET lept)
    if(TESSERACT_FOUND AND LEPTONICA_FOUND)
        set(WAY2TRANS_HAVE_TESSERACT ON)
    endif()
endif()

include(FeatureSummary)
add_feature_info(LayerShellQt WAY2TRANS_HAVE_LAYERSHELL
    "Wayland layer-shell popup/overlay placement (optional, M2)")
add_feature_info(X11Hotkeys WAY2TRANS_HAVE_XCB_KEYSYMS
    "X11 global hotkeys via XGrabKey (xcb + xcb-keysyms; Null fallback otherwise)")
add_feature_info(Translations WAY2TRANS_HAVE_TRANSLATIONS
    "Embedded UI translations via Qt Linguist (English source otherwise)")
add_feature_info(TesseractOCR WAY2TRANS_HAVE_TESSERACT
    "Tesseract OCR backend for screenshot translation (optional, M3)")

# --- Subdirectories --------------------------------------------------------
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

feature_summary(WHAT ALL DESCRIPTION "way2trans configuration summary:")
