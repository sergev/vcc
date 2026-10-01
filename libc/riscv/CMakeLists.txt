#
# RISC-V runtime for bare-metal qemu `virt`: crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a: the assembly leaves here, plus C
# sources (libc/common and the leaves here) compiled by our own toolchain.  Assembled
# with a RISC-V-capable clang and archived with llvm-ar; skipped when those tools are
# absent.
#
find_program(RISCV_CLANG NAMES clang HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
    NO_DEFAULT_PATH)
find_program(RISCV_CLANG NAMES clang)
find_program(RISCV_AR NAMES llvm-ar HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
find_program(RISCV_LD NAMES ld.lld HINTS /opt/homebrew/bin /opt/homebrew/opt/llvm/bin)
find_program(RISCV_QEMU NAMES qemu-system-riscv64)

set(RISCV_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "riscv64")
        set(RISCV_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(RISCV_TOOLS_FOUND ${RISCV_TOOLS_FOUND} CACHE INTERNAL "")
set(RISCV_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(RISCV_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")

if(NOT RISCV_TOOLS_FOUND)
    message(STATUS "No RISC-V clang/llvm-ar: the RISC-V runtime is not built")
    return()
endif()

set(RISCV_ASFLAGS --target=riscv64 -march=rv64imfd -mabi=lp64d -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves.
set(RISCV_LIBC_OBJS "")
foreach(name console malloc)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND RISCV_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genriscv -> clang.  The BESM-6 headers
# serve until RISC-V has its own (R18).
file(GLOB RISCV_LIBC_HEADERS ${BESM6_INCLUDE_DIR}/*.h)
macro(riscv_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${BESM6_INCLUDE_DIR} ${srcdir}/${name}.c
                -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t riscv64 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genriscv ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${RISCV_LIBC_HEADERS} parse lower genriscv
        COMMENT "Compiling ${name}.c -> ${name}.o (RISC-V)")
    list(APPEND RISCV_LIBC_OBJS ${out}/${name}.o)
endmacro()

# printf and friends wait for a portable __doprnt (R19).
set(RISCV_LIBC_C_COMMON ${LIBC_C_COMMON})
list(REMOVE_ITEM RISCV_LIBC_C_COMMON printf sprintf snprintf)
foreach(name ${RISCV_LIBC_C_COMMON})
    riscv_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name frexp ldexp)
    riscv_compile_libc_c(${name} ${src})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${RISCV_LIBC_OBJS}
    DEPENDS ${RISCV_LIBC_OBJS})
add_custom_target(riscv-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
