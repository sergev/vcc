#
# Parser library
#
add_library(parser STATIC
    parser.c
    expr.c
    decl.c
    stmt.c
    nametab.c
)
target_include_directories(parser PUBLIC .)
target_link_libraries(parser scanner ast libutil)

#
# Tests for parser (including the textbook chapter tests)
#
add_executable(parser-tests
    test/simple_tests.cpp
    test/statement_tests.cpp
    test/operator_tests.cpp
    test/type_tests.cpp
    test/struct_tests.cpp
    test/declaration_tests.cpp
    test/constant_tests.cpp
    test/serialize_tests.cpp
    test/negative_tests.cpp
    test/location_tests.cpp
    test/chapter1_tests.cpp
    test/chapter2_tests.cpp
    test/chapter3_tests.cpp
    test/chapter4_tests.cpp
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
target_link_libraries(parser-tests parser GTest::gtest_main)
test_chdir_to_bindir(parser-tests)
gtest_discover_tests(parser-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
