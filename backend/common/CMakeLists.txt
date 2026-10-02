#
# Code shared by every backend: the command-line driver, control flow and liveness,
# register allocation.
#
add_library(backend_common STATIC
    driver.c
    flow.c
    regalloc.c
)
target_include_directories(backend_common PUBLIC .)
target_link_libraries(backend_common tac libutil)

# Header-only test fixture shared by the backend test executables (backend_test.h).
add_library(backend_test_util INTERFACE)
target_include_directories(backend_test_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test)

# The shared "Writing a C Compiler" run tests (test/book/), compiled into each backend's
# test executable against that backend's book_test.h.
file(GLOB BOOK_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/book/chapter*_tests*.cpp)
set(BOOK_TEST_SOURCES ${BOOK_TEST_SOURCES} PARENT_SCOPE)

# Tests of the shared backend code.
add_executable(backend-tests
    test/flow_tests.cpp
)
target_link_libraries(backend-tests backend_common parser translator semantic GTest::gtest_main
    test_util backend_test_util)
target_compile_definitions(backend-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${RISCV_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}")
test_chdir_to_bindir(backend-tests)
gtest_discover_tests(backend-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
