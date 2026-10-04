#
# x86-64 backend library
#
add_library(x86 STATIC
    x86.c
    emit.c
    frame.c
    call.c
    instr.c
    fp.c
    x87.c
    data.c
    codegen.c
)
target_include_directories(x86 PUBLIC .)
target_link_libraries(x86 backend_common tac libutil)

#
# Tests for the x86-64 backend: golden assembly, run tests on qemu, and the shared
# book suite.  Run tests skip themselves when the x86-64 tools are absent.
#

add_executable(x86-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/int_tests.cpp
    test/flow_tests.cpp
    test/call_tests.cpp
    test/data_tests.cpp
    test/fp_tests.cpp
    test/x87_tests.cpp
    test/ptr_tests.cpp
    test/struct_tests.cpp
    test/stdarg_tests.cpp
    test/interop_tests.cpp
    test/book_x86_tests.cpp
    test/printf_tests.cpp
    test/str_tests.cpp
    test/mem_tests.cpp
    test/math_tests.cpp
    test/run_tests.cpp
    ${BOOK_TEST_SOURCES}
)
target_link_libraries(x86-tests x86 parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(X86_TOOLS_FOUND)
    add_dependencies(x86-tests x86-libc)
endif()
# The x86-64 headers, then the LP64 and the shared ones.
target_compile_definitions(x86-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${X86_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${LP64_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    X86_TOOLS_FOUND=$<BOOL:${X86_TOOLS_FOUND}>
    X86_CLANG="${X86_CLANG}"
    X86_LD="${X86_LD}"
    X86_QEMU="${X86_QEMU}"
    X86_GNU_AS="${X86_GNU_AS}"
    X86_LIB_DIR="${X86_LIB_DIR}"
    X86_LINK_SCRIPT="${X86_LINK_SCRIPT}")
target_include_directories(x86-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(x86-tests)
gtest_discover_tests(x86-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
