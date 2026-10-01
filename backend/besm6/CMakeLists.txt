#
# BESM-6 backend library
#
add_library(besm STATIC
    besm_alloc.c
    besm_free.c
    besm_const.c
    besm_mnem.c
    emit_madlen.c
    emit_unix.c
    emit_bemsh.c
    frame.c
    codegen.c
    emit.c
    static.c
    instr.c
    intrinsics.c
    peephole.c
    utf8_to_koi7.c
)
target_include_directories(besm PUBLIC .)
target_link_libraries(besm tac libutil)

# The BESM-6 C runtime library (libc.bin) and the target standard headers are built
# and installed by libc/besm6 (a separate top-level tree), not here — the runtime is
# decoupled from codegen.  The `besm-libc-dubna` target it defines is staged next to the
# besm-tests binary (see below and libc/besm6/CMakeLists.txt).

#
# Tests for the BESM-6 backend
#
add_executable(besm-tests
    test/label_tests.cpp
    test/frame_tests.cpp
    test/codegen_tests.cpp
    test/unix_tests.cpp
    test/unix_link_tests.cpp
    test/unix_run_tests.cpp
    test/bemsh_tests.cpp
    test/bemsh_run_tests.cpp
    test/bemsh_printf_tests.cpp
    test/arith_tests.cpp
    test/copy_tests.cpp
    test/flow_tests.cpp
    test/run_tests.cpp
    test/unary_tests.cpp
    test/convert_tests.cpp
    test/init_tests.cpp
    test/ptr_tests.cpp
    test/struct_tests.cpp
    test/char_tests.cpp
    test/bool_tests.cpp
    test/peephole_tests.cpp
    test/intrinsics_tests.cpp
    test/printf_tests.cpp
    test/funcptr_tests.cpp
    test/stdarg_tests.cpp
    test/book_besm6_tests.cpp
    test/mem_tests.cpp
    test/str_tests.cpp
    test/typedef_tests.cpp
    test/math_tests.cpp
    ${BOOK_TEST_SOURCES}
)
target_link_libraries(besm-tests besm parser translator semantic GTest::gtest_main test_util
    backend_test_util)
add_dependencies(besm-tests besm-libc-dubna besm-libc-unix besm-libc-bemsh)
add_definitions(-DTEST_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(besm-tests PRIVATE
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${BESM6_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}")
target_include_directories(besm-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(besm-tests)
gtest_discover_tests(besm-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)

# The besm-headers standard-library header check moved to libc/besm6 with the headers.
