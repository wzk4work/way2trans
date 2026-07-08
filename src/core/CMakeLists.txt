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
    OcrService.cpp
    OcrService.h
    LibraryService.cpp
    LibraryService.h
    storage/Storage.cpp
    storage/Storage.h
    storage/migrations.cpp
    storage/migrations.h
    storage/HistoryStore.cpp
    storage/HistoryStore.h
    storage/VocabStore.cpp
    storage/VocabStore.h
)

target_include_directories(way2trans_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(way2trans_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        # SQLite-backed history/vocabulary storage (DESIGN.md §8.1).
        Qt6::Sql
        way2trans_engines
        # OcrService picks and drives the OCR backends (DESIGN.md §6).
        way2trans_ocr
)

# core/ deliberately links no UI module.
