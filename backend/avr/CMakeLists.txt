#
# AVR backend library
#
add_library(avr STATIC
    avr_ir.c
    emit.c
    frame.c
    instr.c
    call.c
    relax.c
    data.c
    fp.c
    regalloc.c
    peephole.c
    codegen.c
)
target_include_directories(avr PUBLIC .)
target_link_libraries(avr backend_common tac libutil)

#
# Tests for the AVR backend: golden assembly, run tests on qemu, and the shared book
# suite.  Run tests skip themselves when the AVR tools are absent.
#
# The book chapters the code generator handles so far.
set(AVR_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER AVR_BOOK_SOURCES INCLUDE REGEX "/chapter([1-9]|1[0-9]|20)_tests[0-9]*\\.cpp$")

add_executable(avr-tests
    ../common/test/coro/coro_run_tests.cpp
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/int_tests.cpp
    test/relax_tests.cpp
    test/call_tests.cpp
    test/data_tests.cpp
    test/ptr_tests.cpp
    test/fp_tests.cpp
    test/struct_tests.cpp
    test/stdarg_tests.cpp
    test/interop_tests.cpp
    test/run_tests.cpp
    test/regalloc_tests.cpp
    test/peephole_tests.cpp
    test/printf_tests.cpp
    test/str_tests.cpp
    test/mem_tests.cpp
    test/math_tests.cpp
    ${AVR_BOOK_SOURCES}
)
target_link_libraries(avr-tests avr parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(AVR_TOOLS_FOUND)
    add_dependencies(avr-tests avr-libc)
endif()
target_compile_definitions(avr-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${AVR_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${IP16_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    AVR_TOOLS_FOUND=$<BOOL:${AVR_TOOLS_FOUND}>
    AVR_CLANG="${AVR_CLANG}"
    AVR_CLANG_FOUND=$<BOOL:${AVR_CLANG_FOUND}>
    AVR_GCC="${AVR_GCC}"
    AVR_GCC_FOUND=$<BOOL:${AVR_GCC_FOUND}>
    AVR_LIBGCC="${AVR_LIBGCC}"
    AVR_ASSEMBLER="${AVR_ASSEMBLER}"
    AVR_LLD="${AVR_LD}"
    AVR_LINK_FLAGS="${AVR_LINK_FLAGS}"
    AVR_QEMU="${AVR_QEMU}"
    AVR_LIB_DIR="${AVR_LIB_DIR}"
    AVR_LINK_SCRIPT="${AVR_LINK_SCRIPT}")
target_include_directories(avr-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(avr-tests)
gtest_discover_tests(avr-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
