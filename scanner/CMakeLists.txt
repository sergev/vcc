#
# Scanner library
#
add_library(scanner STATIC
    scanner.c
)
target_include_directories(scanner PUBLIC .)
target_link_libraries(scanner PUBLIC libutil)

#
# Tests for scanner (including the textbook chapter tests)
#
add_executable(scanner-tests
    test/tests.cpp
    test/chapter1_tests.cpp
    test/chapter6_tests.cpp
    test/chapter11_tests.cpp
    test/chapter12_tests.cpp
    test/chapter13_tests.cpp
    test/chapter16_tests.cpp
)
target_link_libraries(scanner-tests scanner GTest::gtest_main)
test_chdir_to_bindir(scanner-tests)
gtest_discover_tests(scanner-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
