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

# --- Optional dependencies (feature-gated, never hard-fail) ----------------
# Platform backends (M1+) will grow here; probing now keeps the feature summary
# honest and lets a minimal environment still build the X11-capable core.
find_package(LayerShellQtInterface QUIET)   # Wayland popup/overlay placement (M2)
find_package(PkgConfig QUIET)

set(WAY2TRANS_HAVE_LAYERSHELL ${LayerShellQtInterface_FOUND})

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

include(FeatureSummary)
add_feature_info(LayerShellQt WAY2TRANS_HAVE_LAYERSHELL
    "Wayland layer-shell popup/overlay placement (optional, M2)")
add_feature_info(X11Hotkeys WAY2TRANS_HAVE_XCB_KEYSYMS
    "X11 global hotkeys via XGrabKey (xcb + xcb-keysyms; Null fallback otherwise)")

# --- Subdirectories --------------------------------------------------------
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

feature_summary(WHAT ALL DESCRIPTION "way2trans configuration summary:")
