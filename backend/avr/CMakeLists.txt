#
# AVR backend library
#
add_library(avr STATIC
    avr_ir.c
    emit.c
    codegen.c
)
target_include_directories(avr PUBLIC .)
target_link_libraries(avr backend_common tac libutil)

#
# Tests for the AVR backend.
#
add_executable(avr-tests
    test/emit_tests.cpp
)
target_link_libraries(avr-tests avr GTest::gtest_main test_util)
target_compile_definitions(avr-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
test_chdir_to_bindir(avr-tests)
gtest_discover_tests(avr-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
