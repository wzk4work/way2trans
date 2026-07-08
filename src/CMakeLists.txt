# Layered libraries (see AGENTS.md "目录结构与分层规则").
# Dependency direction: app -> {ui, core, platform, engines, ocr}; ui -> core;
# engines -> core interface. M1 adds platform/ and engines/ alongside the M0
# core/ui/app; ocr/ arrives in M3.
add_subdirectory(engines)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(ui)
add_subdirectory(app)
