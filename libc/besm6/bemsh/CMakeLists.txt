#
# Bemsh (Cyrillic autocode) BESM-6 C runtime library: assemble the hand-written runtime
# helpers plus the compiled stdout I/O leaves into libbem.bin with besmc (Assembler БЕМШ)
# for the Dubna simulator.
#
# This mirrors ../CMakeLists.txt (the Madlen/libc.bin build) but targets the Bemsh dialect.
# The ~41 hand-written Madlen helpers (LIBC_MADLEN, inherited from the parent scope) are
# hand-translated into Bemsh autocode here, with the same stems (b_NAME.bemsh).  On top of
# those we compile the portable C-level libc (LIBC_C_PORTABLE: printf/doprnt, sprintf/snprintf,
# the string/mem/math routines, atoi, and the stdout chain putchar/putch) plus the two Dubna
# stdout leaves (putbyte -> flush -> _tout) through our own toolchain with `genbesm --bemsh`,
# so a C program run under `*bemsh` has the same hosted-libc surface as the Madlen path.  As on
# the Madlen path, malloc is excluded (its heap needs the Unix end/stack memory map the dubna
# monitor does not provide); getch (the input leaf) is deferred (task B5 follow-up).
#
# Each .bemsh file (hand-written or genbesm-emitted) is a Macro-Bemsh deck: each module
# (СТАРТ ... ФИНИШ) is wrapped in the ВВД$$$ ... КНЦ$$$ input/translate markers that the
# translator requires (besmc feeds the source verbatim after *bemsh and adds none).  besmc
# assembles all decks into one relocatable object library, exactly like the Madlen libc.bin.
#

set(BEMSH_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# The inherited LIBC_MADLEN helper stems, resolved to this directory's .bemsh sources.
list(TRANSFORM LIBC_MADLEN PREPEND ${BEMSH_DIR}/ OUTPUT_VARIABLE BEMSH_SOURCES)
list(TRANSFORM BEMSH_SOURCES APPEND .bemsh)

# Compile one NAME.c (from ${srcdir}) through cpp -> parse -> lower -> genbesm --bemsh into
# NAME.bemsh in the build dir, and record it in BEMSH_C_SOURCES.  The Bemsh counterpart of
# the parent's besm6_compile_libc_c macro (SystemCpp / BESM6_HEADERS come from the root and
# parent scopes; genbesm defaults to Unix output, so --bemsh is requested explicitly).
set(BEMSH_C_SOURCES "")
macro(besm6_compile_libc_c_bemsh name srcdir)
    set(c_src ${srcdir}/${name}.c)
    set(c_pre ${CMAKE_CURRENT_BINARY_DIR}/${name}.i)
    set(c_ast ${CMAKE_CURRENT_BINARY_DIR}/${name}.ast)
    set(c_tac ${CMAKE_CURRENT_BINARY_DIR}/${name}.tac)
    set(c_bem ${CMAKE_CURRENT_BINARY_DIR}/${name}.bemsh)
    add_custom_command(
        OUTPUT  ${c_bem}
        COMMAND ${SystemCpp} -E -nostdinc -I${BEMSH_DIR}/../include -I${LIBC_COMMON_INCLUDE_DIR}
                ${c_src} -o ${c_pre}
        COMMAND parse   ${c_pre} ${c_ast}
        COMMAND lower   ${c_ast} ${c_tac}
        COMMAND genbesm --bemsh ${c_tac} ${c_bem}
        DEPENDS ${c_src} ${BESM6_HEADERS} parse lower genbesm
        COMMENT "Compiling ${name}.c -> ${name}.bemsh")
    list(APPEND BEMSH_C_SOURCES ${c_bem})
endmacro()

# The portable C routines shared by every BESM-6 assembler backend (printf/doprnt,
# sprintf/snprintf, string/mem/math, atoi, the putchar/putch stdout wrappers).  Compiled from
# libc/common or libc/besm6, exactly as the Madlen build does.  malloc is dropped here for
# the same reason the Madlen libc.bin drops it — the dubna monitor supplies no heap memory map.
set(BEMSH_C_PORTABLE ${LIBC_C_PORTABLE})
list(REMOVE_ITEM BEMSH_C_PORTABLE malloc)
foreach(name ${BEMSH_C_PORTABLE})
    libc_c_dir(${name} dir)
    besm6_compile_libc_c_bemsh(${name} ${dir})
endforeach()

# The Dubna-monitor stdout leaves (shared with the Madlen build, compiled from ../madlen).
# _tout is already a hand-written helper; getch (the input leaf) is deferred.
besm6_compile_libc_c_bemsh(putbyte ${BEMSH_DIR}/../madlen)
besm6_compile_libc_c_bemsh(flush   ${BEMSH_DIR}/../madlen)

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/libbem.bin
    COMMAND besmc -c ${BEMSH_SOURCES} ${BEMSH_C_SOURCES} -o ${CMAKE_CURRENT_BINARY_DIR}/libbem.obj
    COMMAND ${CMAKE_COMMAND} -E rename
            ${CMAKE_CURRENT_BINARY_DIR}/libbem.obj
            ${CMAKE_CURRENT_BINARY_DIR}/libbem.bin
    # Stage a copy next to besm-tests (mirrors the Madlen libc.bin copy) for the Bemsh run
    # harness (CompileAndRunBemsh), which chdir()s into build/backend/besm6.  A distinct
    # name (libbem.bin) keeps it separate from the Madlen libc.bin staged there.
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/libbem.bin
            ${CMAKE_BINARY_DIR}/backend/besm6/libbem.bin
    DEPENDS ${BEMSH_SOURCES} ${BEMSH_C_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Assembling BESM-6 Bemsh C runtime library (libbem.bin)")

# Built by the default `make`/`make all` (ALL) alongside the Madlen libc.bin and the Unix
# libruntime.a/libc0.a.
add_custom_target(besm-libc-bemsh ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libbem.bin)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libbem.bin DESTINATION share/vcc/besm6/lib)
