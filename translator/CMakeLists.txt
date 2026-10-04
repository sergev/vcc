#
# Translator library
#
add_library(translator STATIC
    translate.c
    expr.c
    stmt.c
)
target_include_directories(translator PUBLIC .)
target_link_libraries(translator optimize semantic tac ast libutil)

#
# Tests for translate
#
add_executable(translate-tests
    test/decl_tests.cpp
    test/expr_tests.cpp
    test/stmt_tests.cpp
    test/cast_tests.cpp
    test/incdec_tests.cpp
    test/switch_tests.cpp
    test/ptr_tests.cpp
    test/struct_tests.cpp
    test/type_tests.cpp
    test/avr_tests.cpp
)
target_link_libraries(translate-tests parser translator GTest::gtest_main test_util)
target_compile_definitions(translate-tests PRIVATE
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${BESM6_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}")
test_chdir_to_bindir(translate-tests)
gtest_discover_tests(translate-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
