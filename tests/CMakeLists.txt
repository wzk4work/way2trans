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

# --- core: AutostartManager XDG entry sync (hermetic XDG_CONFIG_HOME) -------
add_executable(tst_autostartmanager core/tst_autostartmanager.cpp)
target_link_libraries(tst_autostartmanager PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_autostartmanager COMMAND tst_autostartmanager)
set_tests_properties(tst_autostartmanager PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-autostart;QT_QPA_PLATFORM=offscreen")

# --- core: TextCleaner + LangDetector --------------------------------------
add_executable(tst_textcleaner core/tst_textcleaner.cpp)
target_link_libraries(tst_textcleaner PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_textcleaner COMMAND tst_textcleaner)
set_tests_properties(tst_textcleaner PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_langdetector core/tst_langdetector.cpp)
target_link_libraries(tst_langdetector PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_langdetector COMMAND tst_langdetector)
set_tests_properties(tst_langdetector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- core: TranslateService engine set (enabled flags + configured order) ---
add_executable(tst_translateservice core/tst_translateservice.cpp)
target_link_libraries(tst_translateservice PRIVATE Qt6::Test way2trans_core way2trans_engines)
add_test(NAME tst_translateservice COMMAND tst_translateservice)
set_tests_properties(tst_translateservice PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-ts;QT_QPA_PLATFORM=offscreen")

# --- core: SQLite storage (history + vocabulary), hermetic temp DBs (M4) -----
add_executable(tst_storage core/tst_storage.cpp)
target_link_libraries(tst_storage PRIVATE Qt6::Test way2trans_core)
add_test(NAME tst_storage COMMAND tst_storage)
set_tests_properties(tst_storage PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- engines: response parsing (fixtures, no network) ----------------------
add_executable(tst_googleengine engines/tst_googleengine.cpp)
target_link_libraries(tst_googleengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_googleengine COMMAND tst_googleengine)
set_tests_properties(tst_googleengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bingengine engines/tst_bingengine.cpp)
target_link_libraries(tst_bingengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_bingengine COMMAND tst_bingengine)
set_tests_properties(tst_bingengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_deeplengine engines/tst_deeplengine.cpp)
target_link_libraries(tst_deeplengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_deeplengine COMMAND tst_deeplengine)
set_tests_properties(tst_deeplengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_llmengine engines/tst_llmengine.cpp)
target_link_libraries(tst_llmengine PRIVATE Qt6::Test way2trans_engines way2trans_core)
add_test(NAME tst_llmengine COMMAND tst_llmengine)
set_tests_properties(tst_llmengine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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

# --- platform: screenshot backend selection + X11 grab (M3) -----------------
add_executable(tst_screenshotbackend platform/tst_screenshotbackend.cpp)
target_link_libraries(tst_screenshotbackend PRIVATE Qt6::Test way2trans_platform way2trans_core)
add_test(NAME tst_screenshotbackend COMMAND tst_screenshotbackend)
set_tests_properties(tst_screenshotbackend PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-shot;QT_QPA_PLATFORM=offscreen")

# --- ocr: real Tesseract recognition + language/probe logic (M3) ------------
add_executable(tst_tesseractocr ocr/tst_tesseractocr.cpp)
target_link_libraries(tst_tesseractocr PRIVATE Qt6::Test way2trans_ocr way2trans_core)
add_test(NAME tst_tesseractocr COMMAND tst_tesseractocr)
# Real display so QPainter renders text the same way it does in production; the
# recognition cases self-skip if a needed language pack/font is missing.
set_tests_properties(tst_tesseractocr PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-ocr;QT_QPA_PLATFORM=offscreen")

# --- ui: CaptureOverlay smoke test (M3) -------------------------------------
add_executable(tst_captureoverlay ui/tst_captureoverlay.cpp)
target_link_libraries(tst_captureoverlay PRIVATE Qt6::Test way2trans_ui way2trans_core)
add_test(NAME tst_captureoverlay COMMAND tst_captureoverlay)
set_tests_properties(tst_captureoverlay PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- ui: PopupWindow drag-to-resize ------------------------------------------
add_executable(tst_popupwindow ui/tst_popupwindow.cpp)
target_link_libraries(tst_popupwindow PRIVATE Qt6::Test way2trans_ui way2trans_core)
add_test(NAME tst_popupwindow COMMAND tst_popupwindow)
# Hermetic config: the popup persists its user-dragged size to Settings.
set_tests_properties(tst_popupwindow PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-popup;QT_QPA_PLATFORM=offscreen")

# --- ui: MainWindow sidebar shell smoke test (M4) ---------------------------
add_executable(tst_mainwindow ui/tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE Qt6::Test way2trans_ui way2trans_core)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)
# Hermetic config too: the embedded SettingsPage reads Settings at construction.
set_tests_properties(tst_mainwindow PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config-mw;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-data-mw;QT_QPA_PLATFORM=offscreen")
