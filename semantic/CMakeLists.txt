#
# Translator library
#
add_library(semantic STATIC
    symtab.c
    symtab_print.c
    structtab.c
    structtab_print.c
    typetab.c
    typetab_print.c
    typecheck.c
    expressions.c
    init_normalize.c
    initializers.c
    statements.c
    declarations.c
    type_utils.c
    const_convert.c
    label_loops.c
    resolve_labels.c
    target.c
)
target_include_directories(semantic PUBLIC .)
target_link_libraries(semantic tac ast libutil)

#
# Tests for semantic (including the textbook chapter tests)
#
add_executable(semantic-tests
   test/symtab_tests.cpp
   test/structtab_tests.cpp
   test/typetab_tests.cpp
   test/typecheck_tests.cpp
   test/real_tests.cpp
   test/pipeline_tests.cpp
   test/init_normalize_tests.cpp
   test/label_loops_tests.cpp
   test/const_convert_tests.cpp
   test/coercion_tests.cpp
   test/intrinsics_tests.cpp
   test/chapter3_tests.cpp
   test/chapter5_tests.cpp
   test/chapter6_tests.cpp
   test/chapter7_tests.cpp
   test/chapter8_tests.cpp
   test/chapter9_tests.cpp
   test/chapter10_tests.cpp
   test/chapter11_tests.cpp
   test/chapter12_tests.cpp
   test/chapter13_tests.cpp
   test/chapter14_tests.cpp
   test/chapter15_tests.cpp
   test/chapter16_tests.cpp
   test/chapter17_tests.cpp
   test/chapter18_tests.cpp
)
target_link_libraries(semantic-tests parser semantic GTest::gtest_main test_util)
target_compile_definitions(semantic-tests PRIVATE
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${BESM6_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}")
test_chdir_to_bindir(semantic-tests)
gtest_discover_tests(semantic-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
