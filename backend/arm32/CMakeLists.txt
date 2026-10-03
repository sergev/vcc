#
# ARM32 backend library
#
add_library(arm32 STATIC
    a32.c
    emit.c
    codegen.c
    frame.c
    instr.c
    llong.c
    call.c
)
target_include_directories(arm32 PUBLIC .)
target_link_libraries(arm32 backend_common tac libutil)

#
# Tests for the ARM32 backend: golden assembly, run tests on qemu, and the shared
# book suite.  Run tests skip themselves when the ARM32 tools are absent.
#
# The book chapters the code generator handles so far.
set(ARM32_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER ARM32_BOOK_SOURCES INCLUDE REGEX "/chapter[1-8]_tests\\.cpp$")

add_executable(arm32-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/int_tests.cpp
    test/flow_tests.cpp
    test/llong_tests.cpp
    test/run_tests.cpp
    ${ARM32_BOOK_SOURCES}
)
target_link_libraries(arm32-tests arm32 parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(ARM32_TOOLS_FOUND)
    add_dependencies(arm32-tests arm32-libc)
endif()
# The ARM32 headers come with the code that needs them (Plan.md, V21); until then the
# riscv32 ones, the same ILP32 data model but for wchar_t and long double, serve the
# test programs.
target_compile_definitions(arm32-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${RISCV32_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${ILP32_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    ARM32_TOOLS_FOUND=$<BOOL:${ARM32_TOOLS_FOUND}>
    ARM32_CLANG="${ARM32_CLANG}"
    ARM32_LD="${ARM32_LD}"
    ARM32_QEMU="${ARM32_QEMU}"
    ARM32_LIB_DIR="${ARM32_LIB_DIR}"
    ARM32_LINK_SCRIPT="${ARM32_LINK_SCRIPT}")
target_include_directories(arm32-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(arm32-tests)
gtest_discover_tests(arm32-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
