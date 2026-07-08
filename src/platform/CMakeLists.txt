# platform/ : platform adaptation layer — interfaces + per-backend implementations
# (selection / hotkey / popup). This is the ONLY layer allowed to contain
# platform-detection code (AGENTS.md layering rule). Depends on core/ headers for
# Settings/Logging; must not depend on ui/ or engines/.
add_library(way2trans_platform STATIC
    SelectionSource.h
    X11SelectionSource.cpp
    X11SelectionSource.h
    WlSelectionSource.cpp
    WlSelectionSource.h
    HotkeyBackend.h
    NullHotkeyBackend.cpp
    NullHotkeyBackend.h
    PortalHotkeyBackend.cpp
    PortalHotkeyBackend.h
    PopupPlacement.h
    X11CursorPlacement.cpp
    X11CursorPlacement.h
    PlainPlacement.cpp
    PlainPlacement.h
    PlatformFactory.cpp
    PlatformFactory.h
    # Screenshot backends (DESIGN.md §4.3). X11 grabWindow + Wayland portal +
    # grim fallback. All three compile everywhere (portal/grim just probe/spawn
    # at runtime), so there is no feature gate here.
    screenshot/ScreenshotBackend.h
    screenshot/X11ScreenshotBackend.cpp
    screenshot/X11ScreenshotBackend.h
    screenshot/PortalScreenshotBackend.cpp
    screenshot/PortalScreenshotBackend.h
    screenshot/GrimScreenshotBackend.cpp
    screenshot/GrimScreenshotBackend.h
)

target_include_directories(way2trans_platform
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/screenshot
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(way2trans_platform
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        # PortalHotkeyBackend (Wayland GlobalShortcuts) uses QtDBus.
        Qt6::DBus
)

# --- Optional Wayland layer-shell popup placement --------------------------
# Needs LayerShellQt (target LayerShellQt::Interface). When absent the
# LayerShellPlacement backend is not compiled, the build still succeeds, and
# PlatformFactory falls back to PlainPlacement on Wayland (DESIGN.md §4.4, §11).
if(WAY2TRANS_HAVE_LAYERSHELL)
    target_sources(way2trans_platform PRIVATE
        LayerShellPlacement.cpp
        LayerShellPlacement.h
    )
    target_compile_definitions(way2trans_platform PRIVATE WAY2TRANS_HAVE_LAYERSHELL)
    target_link_libraries(way2trans_platform PRIVATE LayerShellQt::Interface)
    message(STATUS "way2trans: Wayland layer-shell popup placement ENABLED")
else()
    message(STATUS "way2trans: LayerShellQt not found - layer-shell placement DISABLED (Plain fallback)")
endif()

# --- Optional X11 XGrabKey hotkey backend ----------------------------------
# Needs xcb + xcb-keysyms (via pkg-config). When xcb-keysyms is missing the
# backend is not compiled, the build still succeeds, and PlatformFactory falls
# back to the Null backend at runtime (DESIGN.md §4.2, §11 optional-deps rule).
if(WAY2TRANS_HAVE_XCB_KEYSYMS)
    target_sources(way2trans_platform PRIVATE
        X11HotkeyBackend.cpp
        X11HotkeyBackend.h
    )
    target_compile_definitions(way2trans_platform PRIVATE WAY2TRANS_HAVE_XCB_KEYSYMS)
    target_link_libraries(way2trans_platform PRIVATE
        PkgConfig::XCB PkgConfig::XCB_KEYSYMS)
    message(STATUS "way2trans: X11 XGrabKey hotkey backend ENABLED")
else()
    message(STATUS "way2trans: xcb-keysyms not found - X11 hotkey backend DISABLED (Null fallback)")
endif()
