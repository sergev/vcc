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
# Tests for the MMIX backend: golden assembly, run tests on mmix, and the shared book
# suite.  Run tests skip themselves when the MMIX tools are absent.
#
# The book chapters the code generator handles so far.
set(MMIX_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER MMIX_BOOK_SOURCES INCLUDE REGEX "/chapter1_tests\\.cpp$")

add_executable(mmix-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/run_tests.cpp
    ${MMIX_BOOK_SOURCES}
)
target_link_libraries(mmix-tests mmix parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(MMIX_TOOLS_FOUND)
    add_dependencies(mmix-tests mmix-libc)
endif()
target_compile_definitions(mmix-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${MMIX_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${LP64_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    MMIX_TOOLS_FOUND=$<BOOL:${MMIX_TOOLS_FOUND}>
    MMIX_AS="${MMIX_AS}"
    MMIX_LD="${MMIX_LD}"
    MMIX_GCC="${MMIX_GCC}"
    MMIX_SIM="${MMIX_SIM}"
    MMIX_LIB_DIR="${MMIX_LIB_DIR}")
target_include_directories(mmix-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(mmix-tests)
gtest_discover_tests(mmix-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 30)
