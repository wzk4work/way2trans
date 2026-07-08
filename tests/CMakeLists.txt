# tests/ mirrors src/ (AGENTS.md). Qt Test based; skipped gracefully if the
# Qt6::Test module was not found so a minimal build still succeeds.
if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6::Test not found - skipping tests")
    return()
endif()

add_executable(tst_settings core/tst_settings.cpp)
target_link_libraries(tst_settings
    PRIVATE
        Qt6::Test
        way2trans_core
)
add_test(NAME tst_settings COMMAND tst_settings)

# Keep tests hermetic: point config/data at the build tree, not the real
# user home, so a round-trip test never clobbers a developer's config.
set_tests_properties(tst_settings PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config;QT_QPA_PLATFORM=offscreen")

# --- core: TextCleaner + LangDetector --------------------------------------
add_executable(tst_textcleaner core/tst_textcleaner.cpp)
target_link_libraries(tst_textcleaner PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_textcleaner COMMAND tst_textcleaner)
set_tests_properties(tst_textcleaner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_langdetector core/tst_langdetector.cpp)
target_link_libraries(tst_langdetector PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_langdetector COMMAND tst_langdetector)
set_tests_properties(tst_langdetector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- engines: response parsing (fixtures, no network) ----------------------
add_executable(tst_googleengine engines/tst_googleengine.cpp)
target_link_libraries(tst_googleengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_googleengine COMMAND tst_googleengine)
set_tests_properties(tst_googleengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bingengine engines/tst_bingengine.cpp)
target_link_libraries(tst_bingengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_bingengine COMMAND tst_bingengine)
set_tests_properties(tst_bingengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- platform: wl-paste output parsing + factory backend selection ----------
# Pure logic only: no Wayland compositor, no wl-paste subprocess, no real X11.
add_executable(tst_wlselectionsource platform/tst_wlselectionsource.cpp)
target_link_libraries(tst_wlselectionsource PRIVATE Qt6::Test way2trans_platform way2trans_core)
add_test(NAME tst_wlselectionsource COMMAND tst_wlselectionsource)
set_tests_properties(tst_wlselectionsource PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_platformfactory platform/tst_platformfactory.cpp)
target_link_libraries(tst_platformfactory PRIVATE Qt6::Test way2trans_platform way2trans_core)
add_test(NAME tst_platformfactory COMMAND tst_platformfactory)
# Hermetic config + offscreen QPA so factory override reads don't touch real config
# and QGuiApplication::platformName() is neither xcb nor wayland (isolates overrides).
set_tests_properties(tst_platformfactory PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-factory;QT_QPA_PLATFORM=offscreen")
