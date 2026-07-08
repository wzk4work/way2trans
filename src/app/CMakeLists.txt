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
