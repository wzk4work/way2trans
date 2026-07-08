# core/ : platform-agnostic business logic (settings, logging, services...).
# Must not depend on ui/. See AGENTS.md layering rules.
add_library(way2trans_core STATIC
    Logging.cpp
    Logging.h
    Settings.cpp
    Settings.h
)

target_include_directories(way2trans_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(way2trans_core
    PUBLIC
        Qt6::Core
)

# core/ deliberately links no UI module.
