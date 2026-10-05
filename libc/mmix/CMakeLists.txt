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
