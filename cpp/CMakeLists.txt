#
# C preprocessor (John F. Reiser's v7 cpp, modernized to C11; ported from the
# v7besm project's b6cpp).  Built as `cpp`, installed as bin/vcpp.
#
# main() lives in cpp.c; the engine is split across buffer/scan/direct/macro/diag.c
# (sharing intern.h) plus the #if expression lexer/parser (yylex.c/parser.c).
# VCC_SHARE_DIR is where `make install` puts each target's headers; cpp searches
# VCC_SHARE_DIR/<target>/include after the -I directories.
#
add_executable(cpp cpp.c buffer.c scan.c direct.c macro.c diag.c parser.c yylex.c)
target_include_directories(cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cpp PRIVATE VCC_SHARE_DIR="${CMAKE_INSTALL_PREFIX}/share/vcc")
install(PROGRAMS $<TARGET_FILE:cpp> DESTINATION bin RENAME vcpp)

add_subdirectory(test)
