# ocr/ : OCR backend implementations (DESIGN.md §6). Depends on the core/
# interface headers (Settings/Logging) only; must not depend on ui/ or
# platform/. OcrService (which picks a backend) lives in core/ and links this.
#
# The OcrBackend interface and the WeChat backend always compile (WeChat only
# probes/spawns a helper at runtime — it never links a proprietary binary,
# AGENTS.md private-binary rules). The Tesseract backend is gated on
# WAY2TRANS_HAVE_TESSERACT so a minimal environment still builds.
add_library(way2trans_ocr STATIC
    OcrBackend.h
    WeChatOcr.cpp
    WeChatOcr.h
)

target_include_directories(way2trans_ocr
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(way2trans_ocr
    PUBLIC
        Qt6::Core
        Qt6::Gui
)
# ocr/ uses core/ headers (Settings, Logging) but does not link way2trans_core:
# core/ links way2trans_ocr (OcrService lives there), so linking back would form
# a static-archive cycle. The core/ symbols WeChatOcr references are provided at
# the final executable/test link, which pulls in way2trans_core anyway.

if(WAY2TRANS_HAVE_TESSERACT)
    target_sources(way2trans_ocr PRIVATE
        TesseractOcr.cpp
        TesseractOcr.h
    )
    target_compile_definitions(way2trans_ocr PUBLIC WAY2TRANS_HAVE_TESSERACT)
    target_link_libraries(way2trans_ocr PRIVATE PkgConfig::TESSERACT PkgConfig::LEPTONICA)
    message(STATUS "way2trans: Tesseract OCR backend ENABLED")
else()
    message(STATUS "way2trans: libtesseract/leptonica not found - Tesseract OCR DISABLED")
endif()
