# app/ : program bootstrap — single instance, D-Bus service, tray, lifecycle.
# app -> {ui, core, ...}; this is the only layer that wires everything together.
add_executable(way2trans
    main.cpp
    Application.cpp
    Application.h
    DBusService.cpp
    DBusService.h
    TrayIcon.cpp
    TrayIcon.h
)

# The D-Bus service name / app id, injected from the top-level CMakeLists.
target_compile_definitions(way2trans
    PRIVATE WAY2TRANS_APP_ID="${WAY2TRANS_APP_ID}"
)

# Compile the .ts to .qm and embed it under :/i18n so the translator loads from
# the binary with no runtime file lookup. Done in this scope so the generated
# .qm and the resource that consumes it share one directory (WAY2TRANS_TS_FILES
# / WAY2TRANS_HAVE_TRANSLATIONS come from the top-level CMakeLists).
if(WAY2TRANS_HAVE_TRANSLATIONS)
    qt_add_translation(WAY2TRANS_QM_FILES ${WAY2TRANS_TS_FILES})
    qt_add_resources(way2trans way2trans_i18n
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${WAY2TRANS_QM_FILES})
    target_compile_definitions(way2trans PRIVATE WAY2TRANS_HAVE_TRANSLATIONS=1)
endif()

target_include_directories(way2trans
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(way2trans
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        way2trans_core
        way2trans_ui
        way2trans_platform
        way2trans_engines
)
