#
# AVR runtime for bare-metal qemu `arduino-mega` (the ATmega1280; see
# docs/Avr_Backend.md).  Uses the AVR binutils (or clang, ld.lld and llvm-ar);
# qemu-system-avr runs the programs.
#
find_program(AVR_QEMU NAMES qemu-system-avr)

vcc_find_cross(AVR
    PREFIXES avr
    GNU_ASFLAGS -mmcu=atmega1280
    GNU_LDFLAGS -m avr51
    CLANG_TARGET "[\n] *avr +- "
    CLANG_FLAGS --target=avr -mmcu=atmega1280)
set(AVR_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
# avr-gcc, when present, is the book tests' oracle in place of clang, its libgcc.a
# following our libc.a for the helpers GCC's code calls beyond ours.
find_program(AVR_GCC_EXE NAMES avr-gcc HINTS ${VCC_CROSS_HINTS})
set(AVR_GCC_FOUND OFF)
set(AVR_LIBGCC "")
if(AVR_GCC_EXE)
    set(AVR_GCC_FOUND ON)
    execute_process(COMMAND ${AVR_GCC_EXE} -mmcu=atmega1280 -print-libgcc-file-name
        OUTPUT_VARIABLE AVR_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(AVR_GCC_FOUND ${AVR_GCC_FOUND} CACHE INTERNAL "")
set(AVR_GCC "${AVR_GCC_EXE}" CACHE INTERNAL "")
set(AVR_LIBGCC "${AVR_LIBGCC}" CACHE INTERNAL "")
set(AVR_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# The ATmega1280: avr51, 128 KB of flash, a 2-byte PC.

# Standard-library header check, as for the other targets ...
add_test(
    NAME avr-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${AVR_INCLUDE_DIR}
            ${IP16_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME avr-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-tavr
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${AVR_INCLUDE_DIR}
            ${IP16_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers, AVR's own, the 16-bit model's and the shared ones, go into one directory.
file(GLOB AVR_TARGET_HEADERS ${AVR_INCLUDE_DIR}/*.h ${IP16_INCLUDE_DIR}/*.h)
file(GLOB AVR_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${AVR_TARGET_HEADERS} ${AVR_COMMON_HEADERS} DESTINATION share/vcc/avr/include)

if(NOT AVR_TOOLS_FOUND)
    message(STATUS "No AVR binutils or clang: the AVR runtime is not built")
    return()
endif()
if(NOT AVR_QEMU)
    message(STATUS "No qemu-system-avr: the AVR run tests will be skipped")
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

vcc_assemble_crt0(AVR ${src} ${out})
# Assembly leaves, and the integer helpers under their libgcc names.
set(AVR_LIBC_OBJS "")
foreach(name console divmod malloc mul setjmp)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${AVR_AS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND AVR_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genavr -> as, with the AVR headers,
# then the 16-bit model's, then the shared ones.
set(AVR_LIBC_INCLUDES -I${AVR_INCLUDE_DIR} -I${IP16_INCLUDE_DIR} -I${LIBC_COMMON_INCLUDE_DIR})
file(GLOB AVR_LIBC_HEADERS ${AVR_INCLUDE_DIR}/*.h ${IP16_INCLUDE_DIR}/*.h
    ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(avr_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc ${AVR_LIBC_INCLUDES}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t avr ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genavr ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${AVR_AS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${AVR_LIBC_HEADERS} parse lower genavr
        COMMENT "Compiling ${name}.c -> ${name}.o (AVR)")
    list(APPEND AVR_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The shared C library with the printf engine, and the binary32 soft-float runtime;
# from libc/ilp32 the long long division and conversions; the AVR's own C.
foreach(name atoi doprnt fabs fma fmax fmin memchr memcmp memcpy memmove memset printf
        puts putchar snprintf sprintf
        strcat strchr strcmp strcpy strerror strlen strncat strncmp strncpy strrchr strstr
        strtok float32 muldi3)
    avr_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name int64 int64conv)
    avr_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()
foreach(name frexp ldexp modf sqrt)
    avr_compile_libc_c(${name} ${src})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${AVR_AR} rcs ${out}/libc.a ${AVR_LIBC_OBJS}
    DEPENDS ${AVR_LIBC_OBJS})
add_custom_target(avr-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

install(FILES ${out}/crt0.o ${out}/libc.a ${src}/link.ld DESTINATION share/vcc/avr/lib)
