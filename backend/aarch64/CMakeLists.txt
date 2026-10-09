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
    regalloc.c
    peephole.c
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
    ../common/test/coro/coro_run_tests.cpp
    ../common/test/alloca/alloca_run_tests.cpp
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
    test/float128_tests.cpp
    test/regalloc_tests.cpp
    test/peephole_tests.cpp
    test/stdarg_tests.cpp
    test/interop_tests.cpp
    test/run_tests.cpp
    test/printf_tests.cpp
    test/str_tests.cpp
    test/mem_tests.cpp
    test/math_tests.cpp
    ${AARCH64_BOOK_SOURCES}
)
target_link_libraries(aarch64-tests aarch64 parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(AARCH64_TOOLS_FOUND)
    add_dependencies(aarch64-tests aarch64-libc)
endif()
target_compile_definitions(aarch64-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${AARCH64_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${LP64_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    AARCH64_TOOLS_FOUND=$<BOOL:${AARCH64_TOOLS_FOUND}>
    AARCH64_CLANG="${AARCH64_CLANG}"
    AARCH64_CLANG_FOUND=$<BOOL:${AARCH64_CLANG_FOUND}>
    AARCH64_ASSEMBLER="${AARCH64_ASSEMBLER}"
    AARCH64_LD="${AARCH64_LD}"
    AARCH64_LINK_FLAGS="${AARCH64_LINK_FLAGS}"
    AARCH64_QEMU="${AARCH64_QEMU}"
    AARCH64_LIB_DIR="${AARCH64_LIB_DIR}"
    AARCH64_LINK_SCRIPT="${AARCH64_LINK_SCRIPT}")
target_include_directories(aarch64-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(aarch64-tests)
gtest_discover_tests(aarch64-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)

#
# macOS on Apple silicon (genaarch64 --darwin): the Mach-O and Apple ABI goldens, and
# the run, interop, libc and book tests of the bare-metal suite, through the darwin
# fixture (test/darwin_test.h), assembled and linked by the system C compiler and run
# natively.  The runs skip off a Mac on Apple silicon.  Scratch files go to darwin/.
#
set(darwin_dir ${CMAKE_CURRENT_BINARY_DIR}/darwin)
file(MAKE_DIRECTORY ${darwin_dir})
add_executable(aarch64-darwin-tests
    ../common/test/coro/coro_run_tests.cpp
    ../common/test/alloca/alloca_run_tests.cpp
    test/darwin_tests.cpp
    test/interop_tests.cpp
    test/printf_tests.cpp
    test/str_tests.cpp
    test/mem_tests.cpp
    test/math_tests.cpp
    ${AARCH64_BOOK_SOURCES}
    ../common/test/book/signed_char_tests.cpp
)
target_link_libraries(aarch64-darwin-tests aarch64 parser translator semantic
    GTest::gtest_main test_util backend_test_util)
if(AARCH64_DARWIN_CC)
    # The entry of a book program, which prints main's result (crt0-status.o's job).
    add_custom_command(OUTPUT ${darwin_dir}/darwin-status.o
        COMMAND ${AARCH64_DARWIN_CC} -arch arm64 -c -o ${darwin_dir}/darwin-status.o
                ${CMAKE_CURRENT_SOURCE_DIR}/test/darwin_status.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/darwin_status.c)
    add_custom_target(aarch64-darwin-status DEPENDS ${darwin_dir}/darwin-status.o)
    add_dependencies(aarch64-darwin-tests aarch64-darwin-status)
endif()
target_compile_definitions(aarch64-darwin-tests PRIVATE
    AARCH64_DARWIN=1
    TEST_DIR="${darwin_dir}"
    TEST_CPP="${SystemCpp}"
    TEST_OVERLAY_INCLUDE_DIR="${DARWIN_INCLUDE_DIR}"
    TEST_INCLUDE_DIR="${AARCH64_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${LP64_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    AARCH64_DARWIN_CC="${AARCH64_DARWIN_CC}"
    AARCH64_DARWIN_LIB_DIR="${AARCH64_DARWIN_LIB_DIR}")
target_include_directories(aarch64-darwin-tests PRIVATE test)
test_chdir_to_bindir(aarch64-darwin-tests)
gtest_discover_tests(aarch64-darwin-tests TEST_PREFIX darwin. EXTRA_ARGS --gtest_repeat=1
    PROPERTIES TIMEOUT 10)
