#
# C compiler driver (ported from the v7besm project's b6cc).  Built as `cc`,
# installed as bin/vcc.
#
# vcc finds our own passes (vcpp, vparse, vlower, vgen*) in the directory it runs
# from and each target's headers and libraries in ../share/vcc/<target>, so the
# installation is relocatable.  Each target's assembler and linker default to those
# libc/<target> found at configure time (scripts/CrossTools.cmake: GNU binutils, else
# clang and ld.lld; the MMIX ones in libc/mmix), else to whatever is on PATH.
#
add_executable(cc cc.c)
foreach(pair RISCV64:RISCV RISCV32:RISCV32 AARCH64:AARCH64 ARM32:ARM32 X86_64:X86 AVR:AVR
        MSP430:MSP430)
    string(REPLACE ":" ";" pair ${pair})
    list(GET pair 0 def)
    list(GET pair 1 var)
    # MSP430 has no clang triple in vcc: a clang there only through VCC_AS.
    if(${var}_TOOLS_FOUND AND (${var}_GNU OR NOT var STREQUAL "MSP430"))
        list(GET ${var}_AS 0 as)
        target_compile_definitions(cc PRIVATE ${def}_AS="${as}" ${def}_LD="${${var}_LD}"
            ${def}_LDFLAGS="${${var}_LINK_FLAGS}")
    endif()
endforeach()
if(MSP430_TOOLS_FOUND)
    target_compile_definitions(cc PRIVATE MSP430_LIBGCC="${MSP430_LIBGCC}")
endif()
if(MMIX_TOOLS_FOUND)
    target_compile_definitions(cc PRIVATE MMIX_AS="${MMIX_AS}" MMIX_LD="${MMIX_LD}"
        MMIX_LIBGCC="${MMIX_LIBGCC}")
endif()
# The C compilers that assemble and link for the hosted targets (libc/linux).
foreach(var X86_64 AARCH64)
    if(${var}_LINUX_CC)
        target_compile_definitions(cc PRIVATE ${var}_LINUX_CC="${${var}_LINUX_CC}")
    endif()
endforeach()
install(PROGRAMS $<TARGET_FILE:cc> DESTINATION bin RENAME vcc)

add_subdirectory(test)
