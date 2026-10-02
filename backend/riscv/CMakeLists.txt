#
# RISC-V backend library
#
add_library(riscv STATIC
    rv.c
    emit.c
    frame.c
    instr.c
    call.c
    regalloc.c
    peephole.c
    data.c
    codegen.c
    llong.c
)
target_include_directories(riscv PUBLIC .)
target_link_libraries(riscv backend_common tac libutil)

#
# Tests for the RISC-V backend: golden assembly, run tests on qemu, and the shared
# book suite.  Run tests skip themselves when the RISC-V tools are absent.
#
add_executable(riscv-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/instr_tests.cpp
    test/regalloc_tests.cpp
    test/peephole_tests.cpp
    test/data_tests.cpp
    test/run_tests.cpp
    test/interop_tests.cpp
    test/printf_tests.cpp
    test/str_tests.cpp
    test/mem_tests.cpp
    test/math_tests.cpp
    test/float128_tests.cpp
    ${BOOK_TEST_SOURCES}
)
target_link_libraries(riscv-tests riscv parser translator semantic GTest::gtest_main test_util
    backend_test_util)
if(RISCV_TOOLS_FOUND)
    add_dependencies(riscv-tests riscv-libc)
endif()
target_compile_definitions(riscv-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${RISCV_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    RISCV_TOOLS_FOUND=$<BOOL:${RISCV_TOOLS_FOUND}>
    RISCV_CLANG="${RISCV_CLANG}"
    RISCV_LD="${RISCV_LD}"
    RISCV_QEMU="${RISCV_QEMU}"
    RISCV_LIB_DIR="${RISCV_LIB_DIR}"
    RISCV_LINK_SCRIPT="${RISCV_LINK_SCRIPT}")
target_include_directories(riscv-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(riscv-tests)
gtest_discover_tests(riscv-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)

#
# The same harness for RV32: the rv32 run tests, with the riscv32 headers, runtime
# and qemu.  Golden assembly stays rv64 only.
#
add_executable(riscv32-tests
    test/rv32_tests.cpp
    test/llong_tests.cpp
    test/interop_tests.cpp
    test/interop32_tests.cpp
)
target_link_libraries(riscv32-tests riscv parser translator semantic GTest::gtest_main test_util
    backend_test_util)
if(RISCV32_TOOLS_FOUND)
    add_dependencies(riscv32-tests riscv32-libc)
endif()
target_compile_definitions(riscv32-tests PRIVATE
    RISCV_TEST_XLEN=32
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${RISCV32_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    RISCV_TOOLS_FOUND=$<BOOL:${RISCV32_TOOLS_FOUND}>
    RISCV_CLANG="${RISCV_CLANG}"
    RISCV_LD="${RISCV_LD}"
    RISCV_QEMU="${RISCV32_QEMU}"
    RISCV_LIB_DIR="${RISCV32_LIB_DIR}"
    RISCV_LINK_SCRIPT="${RISCV32_LINK_SCRIPT}")
target_include_directories(riscv32-tests PRIVATE test)
test_chdir_to_bindir(riscv32-tests)
gtest_discover_tests(riscv32-tests TEST_PREFIX rv32. EXTRA_ARGS --gtest_repeat=1
    PROPERTIES TIMEOUT 10)
