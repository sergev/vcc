#
# Unix (b6as) BESM-6 target runtime: two archives built with the v7besm toolchain
# (b6as, b6ar, b6ranlib).
#
#   libruntime.a  the compiler's own helper set — the b$* routines the code generator
#                 emits calls to (b$save, b$ret, b$div, b$padd, ...).  These are an
#                 implementation detail of this backend's calling convention and
#                 lowering, so this is the one Unix-path artifact we install.
#   libc0.a       a minimal C standard library — the portable routines (printf/str*/
#                 mem*/malloc/...), the stdout/stdin leaves, and the $77 syscall
#                 leaves.  Built for our own test harnesses only, NOT installed: the
#                 authoritative libc.a, crt0.o, and standard headers come from the
#                 sibling v7besm project.
#
# This mirrors ../CMakeLists.txt (the Madlen/libc.bin build) but targets the Unix
# assembler.  The reusable portable C sources (LIBC_C_PORTABLE, inherited from the parent
# scope) are compiled through cpp -> parse -> lower -> genbesm --unix -> .s and assembled
# with b6as.  The hand-written Madlen helpers (LIBC_MADLEN) are replaced here by
# hand-written b6as .s files with the same stems; the three Dubna leaves
# (putbyte/flush/getch) are stubbed C in this directory.  The tools are invoked by bare
# name on PATH, exactly like dubna/besmc in the Madlen path.  b6ranlib then indexes each
# archive so back-references resolve regardless of member order.
#
# Link order is libc0.a before libruntime.a: libc0 members call b$* helpers, and no
# helper calls back into libc0 (the only cross-references among the helpers are
# b_udiv.s -> b$div/b$mod, both inside libruntime.a).
#

set(UNIX_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PORTABLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The three Dubna monitor leaves, reimplemented for the Unix target: portable
# buffer management in C, backed by the SYS_read/SYS_write leaves below.
set(UNIX_LEAVES putbyte flush getch)

# Hand-written Unix v7 syscall leaves ($77 trap): the read/write primitives the
# C leaves call.  exit ($77 1) rides in on LIBC_MADLEN below (unix/exit.s).
set(UNIX_ASM_LEAVES write read)

# The inherited LIBC_MADLEN list holds two different things: the b$* compiler helpers
# and a few hand-written standard-C routines that happen to be assembly.  Split them.
#
# libruntime.a gets the b$* helpers: LIBC_MADLEN minus the Dubna-only b/tout ($71
# print-line extracode — the Unix path emits stdout through SYS_write) and minus the
# three standard-C stems below.
set(UNIX_RUNTIME_HELPERS ${LIBC_MADLEN})
list(REMOVE_ITEM UNIX_RUNTIME_HELPERS b_tout exit frexp ldexp)

# libc0.a gets the hand-written assembly that is standard C, not a compiler helper:
# exit ($77 1) and the <math.h> exponent pair.
set(UNIX_ASM_LIBC exit frexp ldexp)

# Shipped standard headers the portable C sources #include; an edit to any of them rebuilds.
file(GLOB BESM6_HEADERS ${PORTABLE_DIR}/include/*.h ${LIBC_COMMON_INCLUDE_DIR}/*.h)

# Compile one NAME.c (portable from libc/common or ${PORTABLE_DIR}, or a leaf from here) through
# cpp -> parse -> lower -> genbesm --unix -> b6as into NAME.o, and record it in the
# ${objlist} variable (LIBC0_OBJS or RUNTIME_OBJS).
set(LIBC0_OBJS "")
set(RUNTIME_OBJS "")
macro(besm6_unix_compile_c name srcdir objlist)
    set(c_src ${srcdir}/${name}.c)
    set(c_pre ${CMAKE_CURRENT_BINARY_DIR}/${name}.i)
    set(c_ast ${CMAKE_CURRENT_BINARY_DIR}/${name}.ast)
    set(c_tac ${CMAKE_CURRENT_BINARY_DIR}/${name}.tac)
    set(c_asm ${CMAKE_CURRENT_BINARY_DIR}/${name}.s)
    set(c_obj ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    add_custom_command(
        OUTPUT  ${c_obj}
        COMMAND ${SystemCpp} -E -nostdinc -I${PORTABLE_DIR}/include -I${LIBC_COMMON_INCLUDE_DIR}
                ${c_src} -o ${c_pre}
        COMMAND parse   ${c_pre} ${c_ast}
        COMMAND lower   -t besm6 ${c_ast} ${c_tac}
        COMMAND genbesm --unix ${c_tac} ${c_asm}
        COMMAND b6as -o ${c_obj} ${c_asm}
        DEPENDS ${c_src} ${BESM6_HEADERS} parse lower genbesm
        COMMENT "Compiling ${name}.c -> ${name}.o (Unix)")
    list(APPEND ${objlist} ${c_obj})
endmacro()

# Assemble one hand-written NAME.s into NAME.o, recorded in ${objlist}.
macro(besm6_unix_assemble name objlist)
    set(s_src ${UNIX_DIR}/${name}.s)
    set(s_obj ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
    add_custom_command(
        OUTPUT  ${s_obj}
        COMMAND b6as -o ${s_obj} ${s_src}
        DEPENDS ${s_src}
        COMMENT "Assembling ${name}.s -> ${name}.o (Unix)")
    list(APPEND ${objlist} ${s_obj})
endmacro()

foreach(name ${LIBC_C_PORTABLE})
    libc_c_dir(${name} dir)
    besm6_unix_compile_c(${name} ${dir} LIBC0_OBJS)
endforeach()
foreach(name ${UNIX_LEAVES})
    besm6_unix_compile_c(${name} ${UNIX_DIR} LIBC0_OBJS)
endforeach()
foreach(name ${UNIX_ASM_LEAVES} ${UNIX_ASM_LIBC})
    besm6_unix_assemble(${name} LIBC0_OBJS)
endforeach()
foreach(name ${UNIX_RUNTIME_HELPERS})
    besm6_unix_assemble(${name} RUNTIME_OBJS)
endforeach()

# Archive one object list into NAME.a and stage a copy next to besm-tests (mirroring
# the Madlen libc.bin copy): that binary chdir()s into build/backend/besm6 at startup
# and the run harnesses name the archives by relative path.
macro(besm6_unix_archive name objs)
    # A directory of its own: b6ranlib writes its scratch __.SYMDEF into the cwd, and
    # two archives indexed in parallel in one directory swap symbol tables.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${name}.dir)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${name}.a
        # Rebuild from scratch so a replaced member never leaves a stale copy behind.
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_BINARY_DIR}/${name}.a
        COMMAND b6ar rc ${CMAKE_CURRENT_BINARY_DIR}/${name}.a ${objs}
        # Index the archive so b6ld resolves back-references regardless of member order.
        COMMAND b6ranlib ${CMAKE_CURRENT_BINARY_DIR}/${name}.a
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_BINARY_DIR}/${name}.a
                ${CMAKE_BINARY_DIR}/backend/besm6/${name}.a
        DEPENDS ${objs}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${name}.dir
        COMMENT "Archiving BESM-6 Unix ${name}.a")
endmacro()

besm6_unix_archive(libruntime "${RUNTIME_OBJS}")
besm6_unix_archive(libc0      "${LIBC0_OBJS}")

# crt0.o — the C startup object.  Kept OUT of the archives: b6ld takes the entry point
# from the first linked object's first text word, so crt0.o must be linked ahead of
# everything else as a standalone object (the run harnesses do this).  Staged next to
# besm-tests (mirroring the archives) so that harness — which chdir()s into
# build/backend/besm6 — can pick it up by relative path.  Not installed: v7besm owns
# the real crt0.o.
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/backend/besm6/crt0.o
    COMMAND b6as -o ${CMAKE_CURRENT_BINARY_DIR}/crt0.o ${UNIX_DIR}/crt0.s
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_BINARY_DIR}/crt0.o
            ${CMAKE_BINARY_DIR}/backend/besm6/crt0.o
    DEPENDS ${UNIX_DIR}/crt0.s
    COMMENT "Assembling crt0.s -> crt0.o (Unix)")

# Built by the default `make`/`make all` (ALL) alongside the Madlen libc.bin.
add_custom_target(besm-libc-unix ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libruntime.a
            ${CMAKE_CURRENT_BINARY_DIR}/libc0.a
            ${CMAKE_BINARY_DIR}/backend/besm6/crt0.o)

# Only the compiler's own helper set is installed.  libc0.a and crt0.o stay build-local
# (our test harnesses link them); v7besm supplies the real libc.a and crt0.o.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libruntime.a DESTINATION share/vcc/besm6/lib)
