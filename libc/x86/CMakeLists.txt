#
# x86-64 runtime for bare-metal qemu `microvm` (see docs/X86_64_Backend.md): crt0.o,
# crt0-status.o (prints main's result, for the book run tests) and libc.a: the assembly
# leaves, and the C library compiled by our own code generator.  Uses the x86-64
# binutils (or clang, ld.lld and llvm-ar); qemu-system-x86_64 runs the programs.
#
find_program(X86_QEMU NAMES qemu-system-x86_64)

# The host's own binutils serve on an x86-64 host other than macOS.  The `x86-64` line
# of clang's targets itself, not the 32-bit `x86`.
set(x86_prefixes x86_64-elf x86_64-linux-gnu)
if(NOT APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    list(APPEND x86_prefixes -)
endif()
vcc_find_cross(X86
    PREFIXES ${x86_prefixes}
    GNU_ASFLAGS --64
    CLANG_TARGET "[\n] *x86-64 +- "
    CLANG_FLAGS --target=x86_64-none-elf)
set(X86_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(X86_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# Baseline x86-64 (SSE2), System V psABI, ELF, small code model.

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
# All the headers, the x86-64, the LP64 and the shared ones, go into one directory, as
# for aarch64.  x86-64's own float.h and limits.h (x87 long double, signed char) take
# the place of the LP64 ones.
file(GLOB X86_TARGET_HEADERS ${X86_INCLUDE_DIR}/*.h)
file(GLOB X86_LP64_HEADERS ${LP64_INCLUDE_DIR}/*.h)
foreach(header ${X86_TARGET_HEADERS})
    get_filename_component(name ${header} NAME)
    list(REMOVE_ITEM X86_LP64_HEADERS ${LP64_INCLUDE_DIR}/${name})
endforeach()
file(GLOB X86_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${X86_TARGET_HEADERS} ${X86_LP64_HEADERS} ${X86_COMMON_HEADERS}
        DESTINATION share/vcc/x86_64/include)

if(NOT X86_TOOLS_FOUND)
    message(STATUS "No x86-64 binutils or clang: the x86-64 runtime is not built")
    return()
endif()
if(NOT X86_QEMU)
    message(STATUS "No qemu-system-x86_64: the x86-64 run tests will be skipped")
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

vcc_assemble_crt0(X86 ${src} ${out})
# Assembly leaves.
set(X86_LIBC_OBJS "")
foreach(name console malloc setjmp sqrt)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${X86_AS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND X86_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genx86 -> as, with the x86-64 headers,
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
        COMMAND ${X86_AS} -o ${out}/${name}.o ${out}/${name}.s
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
    COMMAND ${X86_AR} rcs ${out}/libc.a ${X86_LIBC_OBJS}
    DEPENDS ${X86_LIBC_OBJS})
add_custom_target(x86-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the linker script for qemu `microvm`.
install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/x86_64/lib)
