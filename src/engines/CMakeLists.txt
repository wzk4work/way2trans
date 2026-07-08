# engines/ : translation engine implementations. Each engine keeps its web
# request/response marshalling in a single file (DESIGN.md §5.1). Depends on
# core/ interface headers (Settings, Logging) only; must not depend on ui/ or
# platform/ (AGENTS.md). To avoid a static-library link cycle with core/ (which
# owns the engine instances via EngineRegistry) engines pulls core's public
# headers in via the include path; the symbols resolve when the app links both.
add_library(way2trans_engines STATIC
    TranslateTypes.h
    TranslateEngine.h
    EngineRegistry.cpp
    EngineRegistry.h
    GoogleWebEngine.cpp
    GoogleWebEngine.h
    BingWebEngine.cpp
    BingWebEngine.h
)

target_include_directories(way2trans_engines
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(way2trans_engines
    PUBLIC
        Qt6::Core
        Qt6::Network
)
