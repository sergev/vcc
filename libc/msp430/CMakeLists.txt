#
# MSP430 runtime for the mspsim simulator (the classic MSP430; see docs/Msp430_Backend.md).
# Built with the GNU MSP430 binutils: msp430-elf-as (or msp430-unknown-elf-as) assembles,
# -ld links, -ar archives.  msp430-elf-gcc, when present, is the tests' second compiler,
# and its libgcc.a follows our libc.a in a link.  clang's MSP430 target and ld.lld, when
# present, are the tests' third compiler and second assembler.  mspsim runs the programs.
#
find_program(MSPSIM NAMES mspsim HINTS $ENV{HOME}/.local/bin)
# The classic MSP430, no -mmcu: no device files, no hardware multiplier.
vcc_find_cross(MSP430
    PREFIXES msp430-elf msp430-unknown-elf
    GNU_ASFLAGS -mcpu=msp430
    CLANG_TARGET "[\n] *msp430 +- "
    CLANG_FLAGS --target=msp430
    LLD_FLAGS -n)
set(MSP430_OBJCOPY "")
if(MSP430_GNU)
    list(GET MSP430_AS 0 as)
    string(REGEX REPLACE "as$" "objcopy" MSP430_OBJCOPY ${as})
endif()
find_program(MSP430_GCC_EXE NAMES msp430-elf-gcc msp430-unknown-elf-gcc HINTS ${VCC_CROSS_HINTS})
set(MSP430_GCC_FOUND OFF)
set(MSP430_LIBGCC "")
if(MSP430_GCC_EXE AND MSP430_GNU)
    set(MSP430_GCC_FOUND ON)
    # The 430 multilib's libgcc: the helpers GCC's code calls beyond ours.
    execute_process(COMMAND ${MSP430_GCC_EXE} -mcpu=msp430 -print-libgcc-file-name
        OUTPUT_VARIABLE MSP430_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT VCC_LLD)
    set(MSP430_CLANG_FOUND OFF CACHE INTERNAL "")
endif()
# For the backend tests.
set(MSP430_GCC_FOUND ${MSP430_GCC_FOUND} CACHE INTERNAL "")
set(MSP430_GCC ${MSP430_GCC_EXE} CACHE INTERNAL "")
set(MSP430_OBJCOPY ${MSP430_OBJCOPY} CACHE INTERNAL "")
set(MSP430_LIBGCC ${MSP430_LIBGCC} CACHE INTERNAL "")
set(MSP430_LLD ${VCC_LLD} CACHE INTERNAL "")
set(MSP430_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(MSP430_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")

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

# All the headers, MSP430's own, the 16-bit model's and the shared ones, go into one
# directory.  MSP430's stddef.h and stdint.h stand in for the 16-bit model's (wchar_t
# is a long), as they do on the search path.
file(GLOB MSP430_TARGET_HEADERS ${MSP430_INCLUDE_DIR}/*.h)
file(GLOB MSP430_MODEL_HEADERS ${IP16_INCLUDE_DIR}/*.h)
foreach(header ${MSP430_TARGET_HEADERS})
    get_filename_component(name ${header} NAME)
    list(REMOVE_ITEM MSP430_MODEL_HEADERS ${IP16_INCLUDE_DIR}/${name})
endforeach()
file(GLOB MSP430_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${MSP430_TARGET_HEADERS} ${MSP430_MODEL_HEADERS} ${MSP430_COMMON_HEADERS}
    DESTINATION share/vcc/msp430/include)

if(NOT MSP430_TOOLS_FOUND)
    message(STATUS "No MSP430 binutils or clang: the MSP430 runtime is not built")
    return()
endif()
if(NOT MSPSIM)
    message(STATUS "No mspsim: the MSP430 run tests will be skipped")
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

vcc_assemble_crt0(MSP430 ${src} ${out})
# Assembly leaves, and the integer helpers under their MSP430 EABI names.
set(MSP430_LIBC_OBJS "")
foreach(name console divmod mul shift mspabi64 setjmp)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${MSP430_AS} -o ${out}/${name}.o ${src}/${name}.s
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
        COMMAND ${MSP430_AS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${MSP430_LIBC_HEADERS} parse lower genmsp430
        COMMENT "Compiling ${name}.c -> ${name}.o (MSP430)")
    list(APPEND MSP430_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The shared C library, and the binary32 and binary64
# soft-float runtimes; from libc/ilp32 the long long division and conversions and the
# binary64 frexp/ldexp/modf; the MSP430 EABI names GCC and clang call, over all of them.
foreach(name atoi co costack doprnt fabs fma fmax fmin memchr memcmp memcpy memmove memset muldi3 printf
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

install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/msp430/lib)
