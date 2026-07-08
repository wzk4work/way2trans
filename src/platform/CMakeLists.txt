# platform/ : platform adaptation layer — interfaces + per-backend implementations
# (selection / hotkey / popup). This is the ONLY layer allowed to contain
# platform-detection code (AGENTS.md layering rule). Depends on core/ headers for
# Settings/Logging; must not depend on ui/ or engines/.
add_library(way2trans_platform STATIC
    SelectionSource.h
    X11SelectionSource.cpp
    X11SelectionSource.h
    HotkeyBackend.h
    NullHotkeyBackend.cpp
    NullHotkeyBackend.h
    PopupPlacement.h
    X11CursorPlacement.cpp
    X11CursorPlacement.h
    PlainPlacement.cpp
    PlainPlacement.h
    PlatformFactory.cpp
    PlatformFactory.h
)

target_include_directories(way2trans_platform
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(way2trans_platform
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

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
