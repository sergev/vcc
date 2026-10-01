#
# RISC-V runtime for bare-metal qemu `virt`: crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a, assembled with a RISC-V-capable clang
# and archived with llvm-ar.  Skipped when those tools are absent.
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
add_custom_command(OUTPUT ${out}/console.o
    COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -o ${out}/console.o ${src}/console.s
    DEPENDS ${src}/console.s)
add_custom_command(OUTPUT ${out}/string.o
    COMMAND ${RISCV_CLANG} ${RISCV_ASFLAGS} -o ${out}/string.o ${src}/string.s
    DEPENDS ${src}/string.s)
add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${out}/console.o ${out}/string.o
    DEPENDS ${out}/console.o ${out}/string.o)
add_custom_target(riscv-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
