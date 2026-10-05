#
# MSP430 runtime for the mspsim simulator (the classic MSP430; see backend/msp430/Plan.md).
# Built with the GNU MSP430 toolchain: msp430-elf-gcc drives GNU as for the assembly (and
# is the tests' second compiler), msp430-elf-ld links, msp430-elf-ar archives.  clang's
# MSP430 target and ld.lld, when present, are the tests' third compiler and second
# assembler.  mspsim runs the programs.
#
find_program(MSPSIM NAMES mspsim HINTS $ENV{HOME}/.local/bin)
foreach(tool gcc ld ar objcopy)
    string(TOUPPER ${tool} var)
    find_program(MSP430_${var}_EXE NAMES msp430-elf-${tool} HINTS $ENV{HOME}/.local/bin)
endforeach()

set(MSP430_TOOLS_FOUND OFF)
set(MSP430_LIBGCC "")
if(MSP430_GCC_EXE AND MSP430_LD_EXE AND MSP430_AR_EXE AND MSP430_OBJCOPY_EXE)
    set(MSP430_TOOLS_FOUND ON)
    # The 430 multilib's libgcc: the helpers GCC's code calls beyond ours.
    execute_process(COMMAND ${MSP430_GCC_EXE} -mcpu=msp430 -print-libgcc-file-name
        OUTPUT_VARIABLE MSP430_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(MSP430_CLANG_FOUND OFF)
if(RISCV_CLANG AND RISCV_LD)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "[\n] *msp430 +- ")
        set(MSP430_CLANG_FOUND ON)
    endif()
endif()
# For the backend tests.
set(MSP430_TOOLS_FOUND ${MSP430_TOOLS_FOUND} CACHE INTERNAL "")
set(MSP430_GCC ${MSP430_GCC_EXE} CACHE INTERNAL "")
set(MSP430_LD ${MSP430_LD_EXE} CACHE INTERNAL "")
set(MSP430_AR ${MSP430_AR_EXE} CACHE INTERNAL "")
set(MSP430_OBJCOPY ${MSP430_OBJCOPY_EXE} CACHE INTERNAL "")
set(MSP430_LIBGCC ${MSP430_LIBGCC} CACHE INTERNAL "")
set(MSP430_CLANG_FOUND ${MSP430_CLANG_FOUND} CACHE INTERNAL "")
set(MSP430_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(MSP430_LLD ${RISCV_LD} CACHE INTERNAL "")
set(MSP430_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(MSP430_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# The classic MSP430, no -mmcu: no device files, no hardware multiplier.
set(MSP430_TARGET_FLAGS -mcpu=msp430 CACHE INTERNAL "")

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
    message(STATUS "No msp430-elf-gcc/ld/ar/objcopy: the MSP430 runtime is not built")
    return()
endif()
if(NOT MSPSIM)
    message(STATUS "No mspsim: the MSP430 run tests will be skipped")
endif()

set(MSP430_ASFLAGS ${MSP430_TARGET_FLAGS} -c)
set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${MSP430_GCC} ${MSP430_ASFLAGS} -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${MSP430_GCC} ${MSP430_ASFLAGS} -DPRINT_STATUS -o ${out}/crt0-status.o
            ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves, and the integer helpers under their MSP430 EABI names.
set(MSP430_LIBC_OBJS "")
foreach(name console divmod mul shift mspabi64)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${MSP430_GCC} ${MSP430_ASFLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND MSP430_LIBC_OBJS ${out}/${name}.o)
endforeach()


# C sources, through cpp -> parse -> lower -> genmsp430 -> GNU as, with the MSP430 headers,
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
        COMMAND ${MSP430_GCC} ${MSP430_ASFLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${MSP430_LIBC_HEADERS} parse lower genmsp430
        COMMENT "Compiling ${name}.c -> ${name}.o (MSP430)")
    list(APPEND MSP430_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The shared C library, and the binary32 and binary64
# soft-float runtimes; from libc/ilp32 the long long division and conversions and the
# binary64 frexp/ldexp/modf; the MSP430 EABI names GCC and clang call, over all of them.
foreach(name atoi doprnt fabs fma fmax fmin memchr memcmp memcpy memmove memset muldi3 printf
        puts putchar snprintf sprintf
        strcat strchr strcmp strcpy strerror strlen strncat strncmp strncpy strrchr strstr
        strtok float32 float64)
    msp430_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name int64 int64conv frexp ldexp modf)
    msp430_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()
foreach(name mspabif mspabid malloc)
    msp430_compile_libc_c(${name} ${src})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${MSP430_AR} rcs ${out}/libc.a ${MSP430_LIBC_OBJS}
    DEPENDS ${MSP430_LIBC_OBJS})
add_custom_target(msp430-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
