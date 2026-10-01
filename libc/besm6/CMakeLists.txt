#
# BESM-6 C runtime library: assemble the runtime helpers into libc.bin for the
# Dubna simulator.
#
# This is the target runtime, decoupled from the codegen backend (backend/besm6):
# it never links against the `besm` library, it only *runs the finished compiler*
# (parse -> lower -> genbesm -> besmc) over its own C and Madlen sources.
#
# Two kinds of sources:
#   * hand-written Madlen helpers (LIBC_MADLEN) — calling convention, arithmetic,
#     comparisons, byte access, b/tout, exit;
#   * higher-level routines written in C (LIBC_C), compiled by our own toolchain
#     (parse -> lower -> genbesm) into Madlen and assembled alongside the helpers.
#
set(LIBC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/madlen)
set(LIBC_MADLEN
    b_div b_eq b_ge b_gt b_le b_lt b_mod b_mul b_ne b_not
    b_ret b_save0 b_save b_tout exit
    b_udiv b_umod b_ult b_ule b_ugt b_uge b_dtoi b_dtou b_utod b_uneg
    b_lsh b_rsh b_uadd b_usub b_umul
    b_flt b_fle b_fgt b_fge b_stb b_padd b_pinc b_pdec b_pdiff
    ldexp frexp)
list(TRANSFORM LIBC_MADLEN PREPEND ${LIBC_DIR}/ OUTPUT_VARIABLE LIBC_SOURCES)
list(TRANSFORM LIBC_SOURCES APPEND .madlen)

# The system cpp (SystemCpp) and the BESM-6 include dir (BESM6_INCLUDE_DIR) are
# located in the root CMakeLists.txt so every test target can reach them; the libc
# C sources #include the project's own standard headers and so must be run through
# a C preprocessor before our front end.

# Standard headers consumed by the libc sources; an edit to any of them rebuilds.
file(GLOB BESM6_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)

# C runtime routines compiled by our own pipeline.  Each NAME.c becomes NAME.madlen
# in the build directory via cpp -> parse -> lower -> genbesm --madlen.  genbesm now
# defaults to Unix (b6as) output, so the Madlen dialect is requested explicitly;
# besmc only recognizes the .madlen extension, so genbesm writes there.
# Portable routines shared by every BESM-6 assembler backend, compiled from
# libc/common (LIBC_C_COMMON) or, for the BESM-6-specific ones, this directory
# (libc_c_dir picks).  Most have no Dubna dependency — they build on the
# putbyte/flush/getch seam or are pure computation — so libc/besm6/unix and
# libc/besm6/bemsh will reuse them once each supplies its own leaves below.  The
# lone exception is malloc: its heap is provisioned between the linker `end` symbol
# and the b6sim stack base, which only the Unix path supplies, so it is dropped
# from the Madlen libc.bin below (but kept in the Unix libc0.a).
set(LIBC_C_PORTABLE
    printf puts putchar putch
    doprnt sprintf snprintf modf malloc
    fabs fmax fmin fma
    memcpy memset memcmp memmove memchr
    strlen strcpy strncpy strcat strncat
    strcmp strncmp strchr strrchr strstr
    strtok strerror
    atoi)

# The source directory of portable routine `name`.
set(LIBC_BESM6_DIR ${CMAKE_CURRENT_SOURCE_DIR})
macro(libc_c_dir name outvar)
    if(${name} IN_LIST LIBC_C_COMMON)
        set(${outvar} ${LIBC_COMMON_DIR})
    else()
        set(${outvar} ${LIBC_BESM6_DIR})
    endif()
endmacro()

# Dubna monitor glue: the byte-emission/reading leaves, compiled from madlen/.
# A different assembler target reimplements just these three (plus the .madlen
# helpers) against its own OS.
set(LIBC_C_DUBNA
    putbyte flush getch)

# Compile one NAME.c (from ${srcdir}) through cpp -> parse -> lower -> genbesm into
# NAME.madlen in the build dir, and record it in LIBC_C_MADLEN.  A macro (not a
# function) so the list append lands in this directory's scope.
set(LIBC_C_MADLEN "")
macro(besm6_compile_libc_c name srcdir)
    set(c_src  ${srcdir}/${name}.c)
    set(c_pre  ${CMAKE_CURRENT_BINARY_DIR}/${name}.i)
    set(c_ast  ${CMAKE_CURRENT_BINARY_DIR}/${name}.ast)
    set(c_tac  ${CMAKE_CURRENT_BINARY_DIR}/${name}.tac)
    set(c_mad  ${CMAKE_CURRENT_BINARY_DIR}/${name}.madlen)
    add_custom_command(
        OUTPUT  ${c_mad}
        COMMAND ${SystemCpp} -E -nostdinc -I${CMAKE_CURRENT_SOURCE_DIR}/include
                ${c_src} -o ${c_pre}
        COMMAND parse   ${c_pre} ${c_ast}
        COMMAND lower   ${c_ast} ${c_tac}
        COMMAND genbesm --madlen ${c_tac} ${c_mad}
        DEPENDS ${c_src} ${BESM6_HEADERS} parse lower genbesm
        COMMENT "Compiling ${name}.c -> ${name}.madlen")
    list(APPEND LIBC_C_MADLEN ${c_mad})
endmacro()

# malloc/calloc/realloc/free rely on the Unix (b6ld/b6sim) memory map — the `end`
# boundary symbol and the 070000 stack base — which the dubna monitor does not
# provide, so the allocator is excluded from the Madlen libc.bin.  It stays in
# LIBC_C_PORTABLE for the Unix libc0.a build (see unix/CMakeLists.txt).
set(LIBC_C_MADLEN_SOURCES ${LIBC_C_PORTABLE})
list(REMOVE_ITEM LIBC_C_MADLEN_SOURCES malloc)
foreach(name ${LIBC_C_MADLEN_SOURCES})
    libc_c_dir(${name} dir)
    besm6_compile_libc_c(${name} ${dir})
endforeach()
foreach(name ${LIBC_C_DUBNA})
    besm6_compile_libc_c(${name} ${LIBC_DIR})
endforeach()

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/libc.bin
    COMMAND besmc -c ${LIBC_SOURCES} ${LIBC_C_MADLEN} -o ${CMAKE_CURRENT_BINARY_DIR}/libc.obj
    COMMAND ${CMAKE_COMMAND} -E rename
            ${CMAKE_CURRENT_BINARY_DIR}/libc.obj
            ${CMAKE_CURRENT_BINARY_DIR}/libc.bin
    # Stage a copy next to besm-tests: that binary chdir()s into build/backend/besm6
    # at startup and the Dubna job links libc.bin from the current directory.  Done
    # here (in the command, not a POST_BUILD) so it refreshes whenever libc.bin is
    # regenerated.
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/libc.bin
            ${CMAKE_BINARY_DIR}/backend/besm6/libc.bin
    DEPENDS ${LIBC_SOURCES} ${LIBC_C_MADLEN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Assembling BESM-6 C runtime library (libc.bin)")

# Built by the default `make`/`make all` (ALL) so the runtime library is always
# available alongside the compiler and test executables.
add_custom_target(besm-libc-dubna ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libc.bin)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libc.bin DESTINATION share/besm6/lib)

#
# The compiler-owned headers: the C11 freestanding subset (C11 §4) plus besm6.h.
# These describe the compiler's own data model, its <stdarg.h> ABI and its
# intrinsics, so they ship with the compiler that defines them.  The hosted
# headers (stdio.h, string.h, stdlib.h, ...) stay uninstalled -- v7besm's libc.a
# backs those and v7besm ships them.  share/besm6/include is the directory b6cc
# appends to every compilation.
#
install(FILES
    include/besm6.h
    include/float.h
    include/iso646.h
    include/limits.h
    include/stdalign.h
    include/stdarg.h
    include/stdbool.h
    include/stddef.h
    include/stdint.h
    include/stdnoreturn.h
    DESTINATION share/besm6/include)

# The Unix (b6as) runtime archives (libruntime.a, libc0.a).  A child scope so it inherits
# LIBC_C_PORTABLE and LIBC_MADLEN (the reuse surface) set above.
add_subdirectory(unix)

# The Bemsh (Cyrillic autocode) runtime library (libbem.bin).  A child scope so it inherits
# LIBC_C_PORTABLE and LIBC_MADLEN (the reuse surface); see bemsh/CMakeLists.txt.
add_subdirectory(bemsh)

# The target C11 standard headers in include/ are NOT installed: the sibling v7besm
# project ships the authoritative headers.  They stay in the source tree, where the libc
# build above, the besm-headers test below, and the test fixtures (BESM6_INCLUDE_DIR)
# all consume them directly.

#
# Standard-library header check: every header in include/ must preprocess (via the
# system cpp, located in the root CMakeLists) and parse.
#
add_test(
    NAME besm-headers
    COMMAND ${CMAKE_COMMAND} -E env
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/header-check)
