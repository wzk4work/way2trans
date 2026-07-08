# ui/ : windows and widgets. May depend on core/ only (AGENTS.md).
add_library(way2trans_ui STATIC
    MainWindow.cpp
    MainWindow.h
)

target_include_directories(way2trans_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(way2trans_ui
    PUBLIC
        Qt6::Widgets
    PRIVATE
        way2trans_core
)
