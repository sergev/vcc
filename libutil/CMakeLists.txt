#
# Parser library
#
add_library(libutil STATIC
    c_escape.c
    string_map.c
    xalloc.c
    wio.c
    float128.c
    srcloc.c
)
target_include_directories(libutil PUBLIC .)
target_link_libraries(libutil m)

#
# Shared test-only header (test_preprocess.h) lives in test/.  Expose it to the
# test executables of other modules whose fixtures include it, without widening
# the production libutil target's public include path.
#
add_library(test_util INTERFACE)
target_include_directories(test_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test)

#
# Tests for the escape decoder, string map, wio and xalloc
#
add_executable(libutil-tests
    test/c_escape_tests.cpp
    test/string_map_tests.cpp
    test/wio_tests.cpp
    test/xalloc_tests.cpp
    test/float128_tests.cpp
)
target_link_libraries(libutil-tests libutil GTest::gtest_main)
test_chdir_to_bindir(libutil-tests)
gtest_discover_tests(libutil-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
