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
