# Layered libraries (see AGENTS.md "目录结构与分层规则").
# Dependency direction: app -> {ui, core, platform, engines, ocr}; ui -> core.
# M0 only ships core, ui and app; the other layers arrive in later milestones.
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)
