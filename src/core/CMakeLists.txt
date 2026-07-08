# core/ : platform-agnostic business logic (settings, logging, services...).
# Must not depend on ui/ or platform/. Depends on the engines/ interface headers
# only (TranslateService owns engine instances via EngineRegistry, DESIGN.md §5).
add_library(way2trans_core STATIC
    Logging.cpp
    Logging.h
    Settings.cpp
    Settings.h
    LangDetector.cpp
    LangDetector.h
    TextCleaner.cpp
    TextCleaner.h
    TranslateService.cpp
    TranslateService.h
)

target_include_directories(way2trans_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(way2trans_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        way2trans_engines
)

# core/ deliberately links no UI module.
