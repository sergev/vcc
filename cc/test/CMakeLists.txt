#
# End-to-end tests for the vcc driver.  They run the built driver on small
# sources in a throwaway directory, pointing it at the in-tree passes through
# the VCC_* environment overrides, and check what each stage produced.  The
# StagedPrefix cases instead build a miniature installation (bin/ and
# share/vcc/<target>/) and run the driver from it with no overrides, which is
# what checks the relocatable lookup.  Cases that assemble, link or run need
# clang, ld.lld and qemu-system-riscv64, -aarch64, -arm, -x86_64 or -avr (or b6as/b6ld
# for the BESM-6) and are skipped without them.
#
add_executable(cc-tests cc_test.cpp)
target_link_libraries(cc-tests PRIVATE GTest::gtest_main)
test_chdir_to_bindir(cc-tests)
target_compile_definitions(cc-tests PRIVATE
    VCC_COMMAND="$<TARGET_FILE:cc>"
    VCC_CPP_PATH="$<TARGET_FILE:cpp>"
    VCC_PARSE_PATH="$<TARGET_FILE:parse>"
    VCC_LOWER_PATH="$<TARGET_FILE:lower>"
    VCC_GENBESM_PATH="$<TARGET_FILE:genbesm>"
    VCC_GENRISCV_PATH="$<TARGET_FILE:genriscv>"
    VCC_GENAARCH64_PATH="$<TARGET_FILE:genaarch64>"
    VCC_GENARM32_PATH="$<TARGET_FILE:genarm32>"
    VCC_GENX86_PATH="$<TARGET_FILE:genx86>"
    VCC_GENAVR_PATH="$<TARGET_FILE:genavr>"
    BESM6_INCLUDE_DIR="${BESM6_INCLUDE_DIR}"
    RISCV_INCLUDE_DIR="${RISCV_INCLUDE_DIR}"
    RISCV32_INCLUDE_DIR="${RISCV32_INCLUDE_DIR}"
    AARCH64_INCLUDE_DIR="${AARCH64_INCLUDE_DIR}"
    ARM32_INCLUDE_DIR="${ARM32_INCLUDE_DIR}"
    X86_INCLUDE_DIR="${X86_INCLUDE_DIR}"
    AVR_INCLUDE_DIR="${AVR_INCLUDE_DIR}"
    LP64_INCLUDE_DIR="${LP64_INCLUDE_DIR}"
    ILP32_INCLUDE_DIR="${ILP32_INCLUDE_DIR}"
    IP16_INCLUDE_DIR="${IP16_INCLUDE_DIR}"
    COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    RISCV_TOOLS_FOUND=$<BOOL:${RISCV_TOOLS_FOUND}>
    RISCV_CLANG="${RISCV_CLANG}"
    RISCV_LD="${RISCV_LD}"
    RISCV_QEMU="${RISCV_QEMU}"
    RISCV_LIB_DIR="${RISCV_LIB_DIR}"
    RISCV_LINK_SCRIPT="${RISCV_LINK_SCRIPT}"
    RISCV32_QEMU="${RISCV32_QEMU}"
    RISCV32_LIB_DIR="${RISCV32_LIB_DIR}"
    AARCH64_TOOLS_FOUND=$<BOOL:${AARCH64_TOOLS_FOUND}>
    AARCH64_QEMU="${AARCH64_QEMU}"
    AARCH64_LIB_DIR="${AARCH64_LIB_DIR}"
    AARCH64_LINK_SCRIPT="${AARCH64_LINK_SCRIPT}"
    ARM32_TOOLS_FOUND=$<BOOL:${ARM32_TOOLS_FOUND}>
    ARM32_QEMU="${ARM32_QEMU}"
    ARM32_LIB_DIR="${ARM32_LIB_DIR}"
    ARM32_LINK_SCRIPT="${ARM32_LINK_SCRIPT}"
    X86_TOOLS_FOUND=$<BOOL:${X86_TOOLS_FOUND}>
    X86_QEMU="${X86_QEMU}"
    X86_LIB_DIR="${X86_LIB_DIR}"
    X86_LINK_SCRIPT="${X86_LINK_SCRIPT}"
    AVR_TOOLS_FOUND=$<BOOL:${AVR_TOOLS_FOUND}>
    AVR_QEMU="${AVR_QEMU}"
    AVR_LIB_DIR="${AVR_LIB_DIR}"
    AVR_LINK_SCRIPT="${AVR_LINK_SCRIPT}")
add_dependencies(cc-tests cc cpp parse lower genbesm genriscv genaarch64 genarm32 genx86 genavr)
if(RISCV_TOOLS_FOUND)
    add_dependencies(cc-tests riscv-libc)
endif()
if(RISCV32_TOOLS_FOUND)
    add_dependencies(cc-tests riscv32-libc)
endif()
if(AARCH64_TOOLS_FOUND)
    add_dependencies(cc-tests aarch64-libc)
endif()
if(ARM32_TOOLS_FOUND)
    add_dependencies(cc-tests arm32-libc)
endif()
if(X86_TOOLS_FOUND)
    add_dependencies(cc-tests x86-libc)
endif()
if(AVR_TOOLS_FOUND)
    add_dependencies(cc-tests avr-libc)
endif()
gtest_discover_tests(cc-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 30)
