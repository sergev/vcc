#
# RISC-V runtime for bare-metal qemu `virt`: crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a: the assembly leaves here, plus C
# sources (libc/common and the leaves here) compiled by our own toolchain.  Assembled,
# linked and archived by the RISC-V binutils (or clang, ld.lld and llvm-ar); skipped
# when those tools are absent.
#
set(RISCV_PREFIXES riscv64-unknown-elf riscv64-elf riscv64-linux-gnu CACHE INTERNAL "")
vcc_find_cross(RISCV
    PREFIXES ${RISCV_PREFIXES}
    GNU_ASFLAGS -march=rv64imfd -mabi=lp64d
    CLANG_TARGET "riscv64"
    CLANG_FLAGS --target=riscv64 -march=rv64imfd -mabi=lp64d
    RWX_IMAGE)
find_program(RISCV_QEMU NAMES qemu-system-riscv64)

set(RISCV_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(RISCV_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")

# Standard-library header check, like besm-headers: the RISC-V headers and the shared
# ones must preprocess and parse.  Needs no RISC-V tools.
add_test(
    NAME riscv-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${RISCV_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})
# ... and the same through our own preprocessor (cpp/, installed as vcpp).
add_test(
    NAME riscv-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-triscv64
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${RISCV_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers, the RISC-V, the LP64 and the shared ones, go into one directory:
# unlike BESM-6, no other project provides a libc for this target.
file(GLOB RISCV_TARGET_HEADERS ${RISCV_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h)
file(GLOB RISCV_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${RISCV_TARGET_HEADERS} ${RISCV_COMMON_HEADERS} DESTINATION share/vcc/riscv64/include)

if(NOT RISCV_TOOLS_FOUND)
    message(STATUS "No RISC-V binutils or clang: the RISC-V runtime is not built")
    return()
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

vcc_assemble_crt0(RISCV ${src} ${out})
# Assembly leaves.
set(RISCV_LIBC_OBJS "")
foreach(name console malloc sqrt)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_AS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND RISCV_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genriscv -> as.
# float128.c includes the compiler's own binary128 code.
set(RISCV_LIBC_EXTRA_DEPS ${CMAKE_SOURCE_DIR}/libutil/float128.c ${CMAKE_SOURCE_DIR}/libutil/float128.h)
file(GLOB RISCV_LIBC_HEADERS ${RISCV_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h
    ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(riscv_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${RISCV_INCLUDE_DIR} -I${LP64_INCLUDE_DIR}
                -I${LIBC_COMMON_INCLUDE_DIR}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t riscv64 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genriscv ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_AS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${RISCV_LIBC_HEADERS} ${RISCV_LIBC_EXTRA_DEPS} parse lower genriscv
        COMMENT "Compiling ${name}.c -> ${name}.o (RISC-V)")
    list(APPEND RISCV_LIBC_OBJS ${out}/${name}.o)
endmacro()

foreach(name ${LIBC_C_COMMON})
    riscv_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name ${LIBC_C_IEEE})
    riscv_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name ${LIBC_C_LP64})
    riscv_compile_libc_c(${name} ${LIBC_LP64_DIR})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${RISCV_LIBC_OBJS}
    DEPENDS ${RISCV_LIBC_OBJS})
add_custom_target(riscv-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the linker script for qemu `virt`.
install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/riscv64/lib)
