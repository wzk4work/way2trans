# ocr-helper/ : the isolated WeChat OCR helper executable (DESIGN.md §6.1).
#
# This is a SEPARATE process on purpose (AGENTS.md private-binary rule): the
# proprietary WeChat components are loaded here at runtime via dlopen, never in
# the main way2trans process and never bundled. The main app talks to this
# executable solely over a stdio JSON protocol. The helper depends only on
# Qt6::Core (no way2trans libraries) so its surface stays minimal.
add_executable(way2trans-ocr-helper
    main.cpp
)

target_link_libraries(way2trans-ocr-helper
    PRIVATE
        Qt6::Core
        # dl for the runtime dlopen of the user's local WeChat components.
        ${CMAKE_DL_LIBS}
)
