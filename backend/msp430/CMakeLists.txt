#
# MSP430 backend library
#
add_library(msp430 STATIC
    msp_ir.c
    emit.c
    codegen.c
)
target_include_directories(msp430 PUBLIC .)
target_link_libraries(msp430 backend_common tac libutil)

#
# Tests for the MSP430 backend.
#
add_executable(msp430-tests
    test/emit_tests.cpp
)
target_link_libraries(msp430-tests msp430 GTest::gtest_main test_util)
target_compile_definitions(msp430-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    MSP430_TOOLS_FOUND=$<BOOL:${MSP430_TOOLS_FOUND}>
    MSP430_CLANG="${MSP430_CLANG}")
test_chdir_to_bindir(msp430-tests)
gtest_discover_tests(msp430-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
