#
# RISC-V backend library
#
add_library(riscv STATIC
    rv.c
    emit.c
    codegen.c
)
target_include_directories(riscv PUBLIC .)
target_link_libraries(riscv tac libutil)

#
# Tests for the RISC-V backend
#
add_executable(riscv-tests
    test/emit_tests.cpp
)
target_link_libraries(riscv-tests riscv GTest::gtest_main)
target_compile_definitions(riscv-tests PRIVATE TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
test_chdir_to_bindir(riscv-tests)
gtest_discover_tests(riscv-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
