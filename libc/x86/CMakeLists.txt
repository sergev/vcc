#
# x86-64 runtime for bare-metal qemu `microvm` (see backend/x86/Plan.md): crt0.o,
# crt0-status.o (prints main's result, for the book run tests) and libc.a: the assembly
# leaves, and the C library compiled by our own code generator.  Uses the clang, llvm-ar and ld.lld libc/riscv64 found, when
# that clang has the x86-64 target; qemu-system-x86_64 runs the programs; GNU `as` from
# x86_64-elf-binutils, when installed, is a second assembler the tests check our output
# with.
#
find_program(X86_QEMU NAMES qemu-system-x86_64)
find_program(X86_GNU_AS NAMES x86_64-elf-as)

set(X86_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    # The `x86-64` line itself, not the 32-bit `x86`.
    if(targets MATCHES "[\n] *x86-64 +- ")
        set(X86_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(X86_TOOLS_FOUND ${X86_TOOLS_FOUND} CACHE INTERNAL "")
set(X86_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(X86_LD ${RISCV_LD} CACHE INTERNAL "")
set(X86_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(X86_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# Baseline x86-64 (SSE2), System V psABI, ELF, small code model.
set(X86_TARGET_FLAGS --target=x86_64-none-elf CACHE INTERNAL "")

# Standard-library header check, as for aarch64 ...
add_test(
    NAME x86_64-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${X86_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME x86_64-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-tx86_64
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${X86_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})
if(NOT X86_TOOLS_FOUND)
    message(STATUS "No x86-64 clang/llvm-ar: the x86-64 runtime is not built")
    return()
endif()
if(NOT X86_QEMU)
    message(STATUS "No qemu-system-x86_64: the x86-64 run tests will be skipped")
endif()

set(X86_ASFLAGS ${X86_TARGET_FLAGS} -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${X86_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${X86_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves.
set(X86_LIBC_OBJS "")
foreach(name console malloc setjmp)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${X86_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND X86_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genx86 -> clang, with the x86-64 headers,
# then the LP64 and the shared ones.
set(X86_LIBC_INCLUDES -I${X86_INCLUDE_DIR} -I${LP64_INCLUDE_DIR} -I${LIBC_COMMON_INCLUDE_DIR})
file(GLOB X86_LIBC_HEADERS ${X86_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h
     ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(x86_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc ${X86_LIBC_INCLUDES}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t x86_64 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genx86 ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${X86_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${X86_LIBC_HEADERS} parse lower genx86
        COMMENT "Compiling ${name}.c -> ${name}.o (x86-64)")
    list(APPEND X86_LIBC_OBJS ${out}/${name}.o)
endmacro()

# __doprnt, but not the binary128 runtime: long double is the x87 format.
foreach(name ${LIBC_C_COMMON} doprnt)
    x86_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
x86_compile_libc_c(va_arg ${src})
foreach(name ${LIBC_C_LP64})
    x86_compile_libc_c(${name} ${LIBC_LP64_DIR})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${X86_LIBC_OBJS}
    DEPENDS ${X86_LIBC_OBJS})
add_custom_target(x86-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
