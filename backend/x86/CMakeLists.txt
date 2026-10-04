#
# x86-64 backend library
#
add_library(x86 STATIC
    x86.c
    emit.c
    codegen.c
)
target_include_directories(x86 PUBLIC .)
target_link_libraries(x86 backend_common tac libutil)

#
# Tests for the x86-64 backend.
#
add_executable(x86-tests
    test/emit_tests.cpp
)
target_link_libraries(x86-tests x86 GTest::gtest_main test_util)
target_compile_definitions(x86-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
test_chdir_to_bindir(x86-tests)
gtest_discover_tests(x86-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
