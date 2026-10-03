#
# AArch64 runtime for bare-metal qemu `virt`: crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a: the assembly leaves, and the C library
# compiled by our own code generator (see docs/Aarch64_Backend.md).
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
# Standard-library header check, as for riscv64.  Needs no AArch64 tools.
add_test(
    NAME aarch64-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${AARCH64_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})
# ... and the same through our own preprocessor.
add_test(
    NAME aarch64-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-taarch64
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${AARCH64_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers, the AArch64, the LP64 and the shared ones, go into one directory,
# as for riscv64.
file(GLOB AARCH64_TARGET_HEADERS ${AARCH64_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h)
file(GLOB AARCH64_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${AARCH64_TARGET_HEADERS} ${AARCH64_COMMON_HEADERS}
        DESTINATION share/vcc/aarch64/include)

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

# C sources, through cpp -> parse -> lower -> genaarch64 -> clang.
# float128.c includes the compiler's own binary128 code.
set(AARCH64_LIBC_EXTRA_DEPS ${CMAKE_SOURCE_DIR}/libutil/float128.c ${CMAKE_SOURCE_DIR}/libutil/float128.h)
file(GLOB AARCH64_LIBC_HEADERS ${AARCH64_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h
    ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(aarch64_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${AARCH64_INCLUDE_DIR} -I${LP64_INCLUDE_DIR}
                -I${LIBC_COMMON_INCLUDE_DIR}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t aarch64 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genaarch64 ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${AARCH64_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${AARCH64_LIBC_HEADERS} ${AARCH64_LIBC_EXTRA_DEPS} parse lower
                genaarch64
        COMMENT "Compiling ${name}.c -> ${name}.o (AArch64)")
    list(APPEND AARCH64_LIBC_OBJS ${out}/${name}.o)
endmacro()

foreach(name ${LIBC_C_COMMON} ${LIBC_C_IEEE})
    aarch64_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
aarch64_compile_libc_c(va_arg ${src})
foreach(name ${LIBC_C_LP64})
    aarch64_compile_libc_c(${name} ${LIBC_LP64_DIR})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${AARCH64_LIBC_OBJS}
    DEPENDS ${AARCH64_LIBC_OBJS})
add_custom_target(aarch64-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the linker script for qemu `virt`.
install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/aarch64/lib)
