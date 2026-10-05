#
# MMIX backend library
#
add_library(mmix STATIC
    mmix_ir.c
    emit.c
    codegen.c
)
target_include_directories(mmix PUBLIC .)
target_link_libraries(mmix backend_common tac libutil)

#
# Tests for the MMIX backend.
#
add_executable(mmix-tests
    test/emit_tests.cpp
)
target_link_libraries(mmix-tests mmix GTest::gtest_main test_util)
target_compile_definitions(mmix-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    MMIX_TOOLS_FOUND=$<BOOL:${MMIX_TOOLS_FOUND}>
    MMIX_AS="${MMIX_AS}")
test_chdir_to_bindir(mmix-tests)
gtest_discover_tests(mmix-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
