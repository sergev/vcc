#
# C compiler driver (ported from the v7besm project's b6cc).  Built as `cc`,
# installed as bin/vcc.
#
# vcc finds our own passes (vcpp, vparse, vlower, vgen*) in the directory it runs
# from and each target's headers and libraries in ../share/vcc/<target>, so the
# installation is relocatable.  The RISC-V assembler and linker default to the
# clang and ld.lld found at configure time (libc/riscv64/CMakeLists.txt), the MSP430
# ones to the GNU msp430-elf-as and msp430-elf-ld (libc/msp430/CMakeLists.txt), the MMIX
# ones to mmix-knuth-mmixware-as and -ld (libc/mmix/CMakeLists.txt), else to whatever is
# on PATH.
#
add_executable(cc cc.c)
if(RISCV_CLANG AND RISCV_LD)
    target_compile_definitions(cc PRIVATE RISCV_CLANG="${RISCV_CLANG}" RISCV_LD="${RISCV_LD}")
endif()
if(MSP430_TOOLS_FOUND)
    target_compile_definitions(cc PRIVATE MSP430_AS="${MSP430_AS}" MSP430_LD="${MSP430_LD}"
        MSP430_LIBGCC="${MSP430_LIBGCC}")
endif()
if(MMIX_TOOLS_FOUND)
    target_compile_definitions(cc PRIVATE MMIX_AS="${MMIX_AS}" MMIX_LD="${MMIX_LD}"
        MMIX_LIBGCC="${MMIX_LIBGCC}")
endif()
install(PROGRAMS $<TARGET_FILE:cc> DESTINATION bin RENAME vcc)

add_subdirectory(test)
