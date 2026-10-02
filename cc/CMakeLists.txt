#
# C compiler driver (ported from the v7besm project's b6cc).  Built as `cc`,
# installed as bin/vcc.
#
# vcc finds our own passes (vcpp, vparse, vlower, vgen*) in the directory it runs
# from and each target's headers and libraries in ../share/vcc/<target>, so the
# installation is relocatable.  The RISC-V assembler and linker default to the
# clang and ld.lld found at configure time (libc/riscv64/CMakeLists.txt), else to
# whatever is on PATH.
#
add_executable(cc cc.c)
if(RISCV_CLANG AND RISCV_LD)
    target_compile_definitions(cc PRIVATE RISCV_CLANG="${RISCV_CLANG}" RISCV_LD="${RISCV_LD}")
endif()
install(PROGRAMS $<TARGET_FILE:cc> DESTINATION bin RENAME vcc)

add_subdirectory(test)
