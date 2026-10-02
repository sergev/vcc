#
# AArch64 runtime for bare-metal qemu `virt`: crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a.  For now the archive holds only the
# assembly leaves; the C sources join it once the code generator can compile them.
# Uses the clang, llvm-ar and ld.lld libc/riscv64 found; skipped when that clang has no
# AArch64 target.
#
find_program(AARCH64_QEMU NAMES qemu-system-aarch64)

set(AARCH64_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "aarch64")
        set(AARCH64_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(AARCH64_TOOLS_FOUND ${AARCH64_TOOLS_FOUND} CACHE INTERNAL "")
set(AARCH64_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(AARCH64_LD ${RISCV_LD} CACHE INTERNAL "")
set(AARCH64_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(AARCH64_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")

if(NOT AARCH64_TOOLS_FOUND)
    message(STATUS "No AArch64 clang/llvm-ar: the AArch64 runtime is not built")
    return()
endif()

set(AARCH64_ASFLAGS --target=aarch64-none-elf -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${AARCH64_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${AARCH64_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves.
set(AARCH64_LIBC_OBJS "")
foreach(name console malloc)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${AARCH64_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND AARCH64_LIBC_OBJS ${out}/${name}.o)
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${AARCH64_LIBC_OBJS}
    DEPENDS ${AARCH64_LIBC_OBJS})
add_custom_target(aarch64-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
