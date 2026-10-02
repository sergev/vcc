#
# End-to-end unit tests for the C preprocessor: they drive the built cpp
# binary over real input and check the preprocessed output.  The binary path is
# injected at compile time via the C11PP_* generator expressions below.
#
# Every suite shares one harness: test_support.{h,cpp} spawns the tool under
# test in an auto-removed temp dir, and each test_*.cpp file is a C11 (N1570)
# conformance suite keyed to a standard clause, derived from the common
# PreprocessorTest fixture (test_support.h).  The tool under test is selected
# via the C11PP_* macros below.
#
add_executable(cpp-tests
    test_support.cpp
    test_conditional.cpp
    test_include.cpp
    test_limits.cpp
    test_line_error_pragma.cpp
    test_macro_object_function.cpp
    test_paste.cpp
    test_pragma_operator.cpp
    test_predefined_macros.cpp
    test_stringize.cpp
    test_translation_phases.cpp
    test_trigraphs.cpp
    test_utf8_identifiers.cpp
    test_varargs.cpp)
target_link_libraries(cpp-tests PRIVATE GTest::gtest_main)
test_chdir_to_bindir(cpp-tests)
target_compile_definitions(cpp-tests PRIVATE
    # Point the harness at cpp.  It writes to stdout by default and Normalize()
    # drops line markers, so no -P is needed.  -E is a no-op cpp accepts;
    # -nostdinc keeps an installed header tree out of the tests.  cpp's exit
    # status is its error count, so no separate strict flag is required.
    C11PP_COMMAND="$<TARGET_FILE:cpp>"
    C11PP_ARGS="-E,-nostdinc"
    C11PP_STRICT_ARGS="")
add_dependencies(cpp-tests cpp)
gtest_discover_tests(cpp-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
