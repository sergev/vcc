#
# MSP430 runtime for the mspsim simulator (the classic MSP430; see backend/msp430/Plan.md).
# Uses the clang, llvm-ar and ld.lld libc/riscv64 found, when that clang has the MSP430
# target; mspsim runs the programs.
#
find_program(MSPSIM NAMES mspsim HINTS $ENV{HOME}/.local/bin)

set(MSP430_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR AND RISCV_LD)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "[\n] *msp430 +- ")
        set(MSP430_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(MSP430_TOOLS_FOUND ${MSP430_TOOLS_FOUND} CACHE INTERNAL "")
set(MSP430_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(MSP430_AR ${RISCV_AR} CACHE INTERNAL "")
set(MSP430_LD ${RISCV_LD} CACHE INTERNAL "")
set(MSP430_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(MSP430_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# The classic MSP430, no -mmcu: no device files, no hardware multiplier.
set(MSP430_TARGET_FLAGS --target=msp430 CACHE INTERNAL "")

# Standard-library header check, as for the other targets ...
add_test(
    NAME msp430-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${MSP430_INCLUDE_DIR}
            ${IP16_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME msp430-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-tmsp430
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${MSP430_INCLUDE_DIR}
            ${IP16_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

if(NOT MSP430_TOOLS_FOUND)
    message(STATUS "No MSP430 clang/llvm-ar/ld.lld: the MSP430 runtime is not built")
    return()
endif()
if(NOT MSPSIM)
    message(STATUS "No mspsim: the MSP430 run tests will be skipped")
endif()

set(MSP430_ASFLAGS ${MSP430_TARGET_FLAGS} -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${RISCV_CLANG} ${MSP430_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${RISCV_CLANG} ${MSP430_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o
            ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves, and the integer helpers under their MSP430 EABI names.
set(MSP430_LIBC_OBJS "")
foreach(name console divmod mul shift mspabi64)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${RISCV_CLANG} ${MSP430_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND MSP430_LIBC_OBJS ${out}/${name}.o)
endforeach()


# C sources, through cpp -> parse -> lower -> genmsp430 -> clang, with the MSP430 headers,
# then the 16-bit model's, then the shared ones.
set(MSP430_LIBC_INCLUDES -I${MSP430_INCLUDE_DIR} -I${IP16_INCLUDE_DIR} -I${LIBC_COMMON_INCLUDE_DIR})
file(GLOB MSP430_LIBC_HEADERS ${MSP430_INCLUDE_DIR}/*.h ${IP16_INCLUDE_DIR}/*.h
    ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(msp430_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc ${MSP430_LIBC_INCLUDES}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t msp430 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genmsp430 ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${RISCV_CLANG} ${MSP430_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${MSP430_LIBC_HEADERS} parse lower genmsp430
        COMMENT "Compiling ${name}.c -> ${name}.o (MSP430)")
    list(APPEND MSP430_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The shared C library but the variadic part (T16), and the binary32 and binary64
# soft-float runtimes; from libc/ilp32 the long long division and conversions and the
# binary64 frexp/ldexp/modf; the MSP430 EABI names clang calls, over all of them.
foreach(name atoi fabs fma fmax fmin memchr memcmp memcpy memmove memset muldi3 puts putchar
        strcat strchr strcmp strcpy strerror strlen strncat strncmp strncpy strrchr strstr
        strtok float32 float64)
    msp430_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name int64 frexp ldexp modf)
    msp430_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()
foreach(name mspabi malloc)
    msp430_compile_libc_c(${name} ${src})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${RISCV_AR} rcs ${out}/libc.a ${MSP430_LIBC_OBJS}
    DEPENDS ${MSP430_LIBC_OBJS})
add_custom_target(msp430-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
