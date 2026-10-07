#
# WebAssembly backend library
#
add_library(wasm STATIC
    wasm_ir.c
    emit.c
    frame.c
    instr.c
    data.c
    structure.c
    call.c
    codegen.c
)
target_include_directories(wasm PUBLIC .)
target_link_libraries(wasm backend_common tac libutil)

#
# Tests for the wasm32 backend: golden assembly, run tests under node, and the shared book
# suite.  Run tests skip themselves when clang, wasm-ld or node is absent.
#
# The book chapters the code generator handles so far.
set(WASM_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER WASM_BOOK_SOURCES INCLUDE REGEX "/(chapter([1-9]|1[0-7])_tests|chapter18_tests[0-9])\\.cpp$")

add_executable(wasm32-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/int_tests.cpp
    test/flow_tests.cpp
    test/data_tests.cpp
    test/call_tests.cpp
    test/frame_tests.cpp
    test/ptr_tests.cpp
    test/struct_tests.cpp
    test/stdarg_tests.cpp
    test/interop_tests.cpp
    ../common/test/book/signed_char_tests.cpp
    test/run_tests.cpp
    ${WASM_BOOK_SOURCES}
)
target_link_libraries(wasm32-tests wasm parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(WASM32_TOOLS_FOUND)
    add_dependencies(wasm32-tests wasm32-libc)
endif()
string(JOIN " " wasm32_target_flags ${WASM32_TARGET_FLAGS})
target_compile_definitions(wasm32-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${WASM32_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${ILP32_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    WASM32_TOOLS_FOUND=$<BOOL:${WASM32_TOOLS_FOUND}>
    WASM32_CLANG_FOUND=$<BOOL:${WASM32_CLANG_FOUND}>
    WASM32_CLANG="${WASM32_CLANG}"
    WASM32_TARGET_FLAGS="${wasm32_target_flags}"
    WASM32_ASSEMBLER="${WASM32_ASSEMBLER}"
    WASM32_LD="${WASM32_LD}"
    WASM32_LINK_FLAGS="${WASM32_LINK_FLAGS}"
    WASM32_NODE="${WASM32_NODE}"
    WASM32_RUNNER="${WASM32_RUNNER}"
    WASM32_LIB_DIR="${WASM32_LIB_DIR}")
target_include_directories(wasm32-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(wasm32-tests)
gtest_discover_tests(wasm32-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 30)
