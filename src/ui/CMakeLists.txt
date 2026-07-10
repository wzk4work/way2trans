# ui/ : windows and widgets. May depend on core/ only (AGENTS.md). Uses the
# engines/ value types (TranslateTypes.h) via the include path but never links
# an engine implementation.
add_library(way2trans_ui STATIC
    MainWindow.cpp
    MainWindow.h
    EngineResultCard.cpp
    EngineResultCard.h
    PopupWindow.cpp
    PopupWindow.h
    CaptureOverlay.cpp
    CaptureOverlay.h
    HistoryModel.cpp
    HistoryModel.h
    VocabModel.cpp
    VocabModel.h
    ServicesPage.cpp
    ServicesPage.h
    SettingsSections.cpp
    SettingsSections.h
)

target_include_directories(way2trans_ui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(way2trans_ui
    PUBLIC
        Qt6::Widgets
    PRIVATE
        way2trans_core
)
