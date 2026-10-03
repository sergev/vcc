#
# AArch64 backend library
#
add_library(aarch64 STATIC
    a64.c
    emit.c
    frame.c
    instr.c
    call.c
    data.c
    codegen.c
)
target_include_directories(aarch64 PUBLIC .)
target_link_libraries(aarch64 backend_common tac libutil)

#
# Tests for the AArch64 backend: golden assembly, run tests on qemu, and the shared
# book suite.  Run tests skip themselves when the AArch64 tools are absent.
#
# The book chapters the code generator handles so far.
set(AARCH64_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER AARCH64_BOOK_SOURCES INCLUDE REGEX "/chapter[0-9]+_tests[0-9]*\\.cpp$")

add_executable(aarch64-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/int_tests.cpp
    test/flow_tests.cpp
    test/call_tests.cpp
    test/data_tests.cpp
    test/fp_tests.cpp
    test/ptr_tests.cpp
    test/struct_tests.cpp
    test/hfa_tests.cpp
    test/stdarg_tests.cpp
    test/interop_tests.cpp
    test/run_tests.cpp
    ${AARCH64_BOOK_SOURCES}
)
target_link_libraries(aarch64-tests aarch64 parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(AARCH64_TOOLS_FOUND)
    add_dependencies(aarch64-tests aarch64-libc)
endif()
# The AArch64 headers come with the code that needs them (Plan.md, A22): so far
# stdarg.h, ahead of the riscv64 ones, the same LP64 data model.
target_compile_definitions(aarch64-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_TARGET_INCLUDE_DIR="${AARCH64_INCLUDE_DIR}"
    TEST_INCLUDE_DIR="${RISCV_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    AARCH64_TOOLS_FOUND=$<BOOL:${AARCH64_TOOLS_FOUND}>
    AARCH64_CLANG="${AARCH64_CLANG}"
    AARCH64_LD="${AARCH64_LD}"
    AARCH64_QEMU="${AARCH64_QEMU}"
    AARCH64_LIB_DIR="${AARCH64_LIB_DIR}"
    AARCH64_LINK_SCRIPT="${AARCH64_LINK_SCRIPT}")
target_include_directories(aarch64-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(aarch64-tests)
gtest_discover_tests(aarch64-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
