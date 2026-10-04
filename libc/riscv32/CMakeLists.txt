#
# RISC-V RV32 runtime for bare-metal qemu `virt`, like libc/riscv64: crt0.o,
# crt0-status.o and libc.a, with the 32-bit crt0 and malloc, the console and linker
# script of riscv64, and the C sources compiled by our own toolchain for riscv32.
# Uses the clang and llvm-ar libc/riscv64 found; skipped when they cannot target
# riscv32.
#
find_program(RISCV32_QEMU NAMES qemu-system-riscv32)

set(RISCV32_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "riscv32")
        set(RISCV32_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(RISCV32_TOOLS_FOUND ${RISCV32_TOOLS_FOUND} CACHE INTERNAL "")
set(RISCV32_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(RISCV32_LINK_SCRIPT ${RISCV64_DIR}/link.ld CACHE INTERNAL "")

# Standard-library header check, as for riscv64.
add_test(
    NAME riscv32-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${RISCV32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers, as for riscv64.
file(GLOB RISCV32_TARGET_HEADERS ${RISCV32_INCLUDE_DIR}/*.h ${ILP32_INCLUDE_DIR}/*.h)
file(GLOB RISCV32_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${RISCV32_TARGET_HEADERS} ${RISCV32_COMMON_HEADERS}
        DESTINATION share/vcc/riscv32/include)

# ... and the same through our own preprocessor.
add_test(
    NAME riscv32-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-triscv32
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${RISCV32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

if(NOT RISCV32_TOOLS_FOUND)
    message(STATUS "No riscv32 clang/llvm-ar: the RISC-V RV32 runtime is not built")
    return()
endif()

set(RISCV32_ASFLAGS --target=riscv32 -march=rv32imfd -mabi=ilp32d -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${RISCV32_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${RISCV32_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves: the console and sqrt are the same for both widths.
set(RISCV32_LIBC_OBJS "")
foreach(asm ${RISCV64_DIR}/console.s ${RISCV64_DIR}/sqrt.s ${src}/malloc.s)
    get_filename_component(name ${asm} NAME_WE)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${RISCV32_ASFLAGS} -o ${out}/${name}.o ${asm}
        DEPENDS ${asm})
    list(APPEND RISCV32_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genriscv --rv32 -> clang.
set(RISCV32_LIBC_EXTRA_DEPS ${CMAKE_SOURCE_DIR}/libutil/float128.c ${CMAKE_SOURCE_DIR}/libutil/float128.h)
file(GLOB RISCV32_LIBC_HEADERS ${RISCV32_INCLUDE_DIR}/*.h ${ILP32_INCLUDE_DIR}/*.h
     ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(riscv32_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${RISCV32_INCLUDE_DIR} -I${ILP32_INCLUDE_DIR}
                -I${LIBC_COMMON_INCLUDE_DIR}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t riscv32 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genriscv --rv32 ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${RISCV32_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${RISCV32_LIBC_HEADERS} ${RISCV32_LIBC_EXTRA_DEPS} parse lower
                genriscv
        COMMENT "Compiling ${name}.c -> ${name}.o (RISC-V RV32)")
    list(APPEND RISCV32_LIBC_OBJS ${out}/${name}.o)
endmacro()

foreach(name ${LIBC_C_COMMON})
    riscv32_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
# float128.c includes the compiler's own binary128.
foreach(name ${LIBC_C_IEEE})
    riscv32_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name ${LIBC_C_ILP32})
    riscv32_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${RISCV32_LIBC_OBJS}
    DEPENDS ${RISCV32_LIBC_OBJS})
add_custom_target(riscv32-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the linker script for qemu `virt`.
install(FILES ${out}/crt0.o ${out}/libc.a ${RISCV64_DIR}/link.ld DESTINATION share/vcc/riscv32/lib)
