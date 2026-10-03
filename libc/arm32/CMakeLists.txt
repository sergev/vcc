#
# ARM32 runtime for bare-metal qemu `virt` (see docs/Arm32_Backend.md): crt0.o,
# crt0-status.o (prints main's result, for the book run tests) and libc.a: the
# assembly leaves, and the C library compiled by our own code generator.  Uses the
# clang, llvm-ar and ld.lld libc/riscv64 found, when that clang has the 32-bit ARM
# target; qemu-system-arm runs the programs.
#
find_program(ARM32_QEMU NAMES qemu-system-arm)

set(ARM32_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    # The `arm` line itself, not `arm64` or `armeb`.
    if(targets MATCHES "[\n] *arm +- ")
        set(ARM32_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(ARM32_TOOLS_FOUND ${ARM32_TOOLS_FOUND} CACHE INTERNAL "")
set(ARM32_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(ARM32_LD ${RISCV_LD} CACHE INTERNAL "")
set(ARM32_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(ARM32_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# ARMv7-A with hardware divide, ARM state, AAPCS-VFP; our code uses VFPv3-D16 only.
set(ARM32_TARGET_FLAGS --target=armv7a-none-eabihf -mcpu=cortex-a15 -mfpu=vfpv3-d16
    CACHE INTERNAL "")

# Standard-library header check, as for riscv32 ...
add_test(
    NAME arm32-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${ARM32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME arm32-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-tarm32
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${ARM32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers, the ARM32, the ILP32 and the shared ones, go into one directory,
# as for riscv32.
file(GLOB ARM32_TARGET_HEADERS ${ARM32_INCLUDE_DIR}/*.h ${ILP32_INCLUDE_DIR}/*.h)
file(GLOB ARM32_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${ARM32_TARGET_HEADERS} ${ARM32_COMMON_HEADERS}
        DESTINATION share/vcc/arm32/include)

if(NOT ARM32_TOOLS_FOUND)
    message(STATUS "No ARM32 clang/llvm-ar: the ARM32 runtime is not built")
    return()
endif()
if(NOT ARM32_QEMU)
    message(STATUS "No qemu-system-arm: the ARM32 run tests will be skipped")
endif()

set(ARM32_ASFLAGS ${ARM32_TARGET_FLAGS} -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${ARM32_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${ARM32_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves.
set(ARM32_LIBC_OBJS "")
foreach(name console malloc aeabi_divmod aeabi_long aeabi_conv aeabi_mem)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${ARM32_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND ARM32_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genarm32 -> clang.
file(GLOB ARM32_LIBC_HEADERS ${ARM32_INCLUDE_DIR}/*.h ${ILP32_INCLUDE_DIR}/*.h
     ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(arm32_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${ARM32_INCLUDE_DIR} -I${ILP32_INCLUDE_DIR}
                -I${LIBC_COMMON_INCLUDE_DIR}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t arm32 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genarm32 ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${ARM32_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${ARM32_LIBC_HEADERS} parse lower genarm32
        COMMENT "Compiling ${name}.c -> ${name}.o (ARM32)")
    list(APPEND ARM32_LIBC_OBJS ${out}/${name}.o)
endmacro()

# No float128: long double is a double here.
foreach(name ${LIBC_C_COMMON} doprnt)
    arm32_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name ${LIBC_C_ILP32})
    arm32_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${ARM32_LIBC_OBJS}
    DEPENDS ${ARM32_LIBC_OBJS})
add_custom_target(arm32-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the linker script for qemu `virt`.
install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/arm32/lib)
