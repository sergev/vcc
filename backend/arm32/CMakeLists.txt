#
# ARM32 backend library
#
add_library(arm32 STATIC
    a32.c
    emit.c
    codegen.c
)
target_include_directories(arm32 PUBLIC .)
target_link_libraries(arm32 backend_common tac libutil)

#
# Tests for the ARM32 backend.
#
add_executable(arm32-tests
    test/emit_tests.cpp
)
target_link_libraries(arm32-tests arm32 GTest::gtest_main test_util)
target_compile_definitions(arm32-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
test_chdir_to_bindir(arm32-tests)
gtest_discover_tests(arm32-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
