#
# MMIX runtime and headers (see backend/mmix/Plan.md).  Built with the GNU MMIX
# toolchain: mmix-knuth-mmixware-as assembles (with -x -no-predefined-syms, the flags GCC
# passes it), -ld links into Knuth's .mmo, -ar archives, and -gcc with newlib is the tests'
# second compiler.  Knuth's simulator mmix runs the programs.
#
find_program(MMIX_SIM_EXE NAMES mmix HINTS /opt/homebrew/bin $ENV{HOME}/.local/bin)
foreach(tool gcc as ld ar)
    string(TOUPPER ${tool} var)
    find_program(MMIX_${var}_EXE NAMES mmix-knuth-mmixware-${tool} HINTS $ENV{HOME}/.local/bin)
endforeach()

set(MMIX_TOOLS_FOUND OFF)
if(MMIX_GCC_EXE AND MMIX_AS_EXE AND MMIX_LD_EXE AND MMIX_AR_EXE AND MMIX_SIM_EXE)
    set(MMIX_TOOLS_FOUND ON)
else()
    message(STATUS "No mmix-knuth-mmixware-gcc/as/ld/ar or mmix: the MMIX runtime is not built")
endif()
# For the backend tests.
set(MMIX_TOOLS_FOUND ${MMIX_TOOLS_FOUND} CACHE INTERNAL "")
set(MMIX_GCC ${MMIX_GCC_EXE} CACHE INTERNAL "")
set(MMIX_AS ${MMIX_AS_EXE} CACHE INTERNAL "")
set(MMIX_LD ${MMIX_LD_EXE} CACHE INTERNAL "")
set(MMIX_AR ${MMIX_AR_EXE} CACHE INTERNAL "")
set(MMIX_SIM ${MMIX_SIM_EXE} CACHE INTERNAL "")
set(MMIX_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
# The assembler's flags, as GCC passes them: -x lets the assembler and the linker expand
# an out-of-range branch, geta, pushj or jmp and allocate the base registers.
set(MMIX_AS_FLAGS -x -no-predefined-syms CACHE INTERNAL "")
# The linker's, as GCC passes them: text from 0x100, where the .mmo loader expects it
# (from 0, the image does not start).  The output is .mmo, the default emulation.
set(MMIX_LD_FLAGS --defsym __.MMIX.start..text=0x100 CACHE INTERNAL "")

# Standard-library header check, as for the other targets ...
add_test(
    NAME mmix-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${MMIX_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME mmix-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-tmmix
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${MMIX_INCLUDE_DIR}
            ${LP64_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

if(NOT MMIX_TOOLS_FOUND)
    return()
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

# crt0.S goes through GCC for the preprocessor, which passes the assembler MMIX_AS_FLAGS.
add_custom_command(OUTPUT ${out}/crt0.o
    COMMAND ${MMIX_GCC} -c -o ${out}/crt0.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
add_custom_command(OUTPUT ${out}/crt0-status.o
    COMMAND ${MMIX_GCC} -c -DPRINT_STATUS -o ${out}/crt0-status.o ${src}/crt0.S
    DEPENDS ${src}/crt0.S)
# Assembly leaves.  No arithmetic helpers: multiply, divide and binary64 are instructions.
set(MMIX_LIBC_OBJS "")
foreach(name console)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${MMIX_AS} ${MMIX_AS_FLAGS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND MMIX_LIBC_OBJS ${out}/${name}.o)
endforeach()

# The runtime on its own: console I/O, the exit report and the status.
add_test(
    NAME mmix-runtime
    COMMAND ${src}/test/console_test.sh ${MMIX_AS} ${MMIX_LD} ${MMIX_SIM} ${out} ${src}/test
            ${out}/runtime-test)

# C sources, through cpp -> parse -> lower -> genmmix -> GNU as, with the MMIX headers,
# then the LP64 model's, then the shared ones.
set(MMIX_LIBC_INCLUDES -I${MMIX_INCLUDE_DIR} -I${LP64_INCLUDE_DIR} -I${LIBC_COMMON_INCLUDE_DIR})
file(GLOB MMIX_LIBC_HEADERS ${MMIX_INCLUDE_DIR}/*.h ${LP64_INCLUDE_DIR}/*.h
    ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(mmix_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc ${MMIX_LIBC_INCLUDES}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t mmix ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genmmix ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${MMIX_AS} ${MMIX_AS_FLAGS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${MMIX_LIBC_HEADERS} parse lower genmmix
        COMMENT "Compiling ${name}.c -> ${name}.o (MMIX)")
    list(APPEND MMIX_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The shared C library with the __doprnt printf engine (long double is double, as on
# ARM32); from libc/lp64 the binary64 frexp/ldexp/modf.  No integer or soft-float
# helpers: they are instructions here.
foreach(name ${LIBC_C_COMMON} doprnt)
    mmix_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
foreach(name ${LIBC_C_LP64})
    mmix_compile_libc_c(${name} ${LIBC_LP64_DIR})
endforeach()
foreach(name malloc)
    mmix_compile_libc_c(${name} ${src})
endforeach()

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${MMIX_AR} rcs ${out}/libc.a ${MMIX_LIBC_OBJS}
    DEPENDS ${MMIX_LIBC_OBJS})
add_custom_target(mmix-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)
