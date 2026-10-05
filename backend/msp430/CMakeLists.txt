#
# MSP430 backend library
#
add_library(msp430 STATIC
    msp_ir.c
    emit.c
    frame.c
    instr.c
    call.c
    relax.c
    data.c
    fp.c
    codegen.c
)
target_include_directories(msp430 PUBLIC .)
target_link_libraries(msp430 backend_common tac libutil)

#
# Tests for the MSP430 backend: golden assembly, run tests on mspsim, and the shared
# book suite.  Run tests skip themselves when the MSP430 tools are absent.
#
# The book chapters the code generator handles so far.
set(MSP430_BOOK_SOURCES ${BOOK_TEST_SOURCES})
list(FILTER MSP430_BOOK_SOURCES INCLUDE REGEX "/chapter([1-4]|1[12])_tests[0-9]*\\.cpp$")

add_executable(msp430-tests
    test/emit_tests.cpp
    test/codegen_tests.cpp
    test/frame_tests.cpp
    test/int_tests.cpp
    test/run_tests.cpp
    ${MSP430_BOOK_SOURCES}
)
target_link_libraries(msp430-tests msp430 parser translator semantic GTest::gtest_main
    test_util backend_test_util)
if(MSP430_TOOLS_FOUND)
    add_dependencies(msp430-tests msp430-libc)
endif()
target_compile_definitions(msp430-tests PRIVATE
    TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${MSP430_INCLUDE_DIR}"
    TEST_MODEL_INCLUDE_DIR="${IP16_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}"
    MSP430_TOOLS_FOUND=$<BOOL:${MSP430_TOOLS_FOUND}>
    MSP430_CLANG="${MSP430_CLANG}"
    MSP430_LLD="${MSP430_LD}"
    MSPSIM="${MSPSIM}"
    MSP430_LIB_DIR="${MSP430_LIB_DIR}"
    MSP430_LINK_SCRIPT="${MSP430_LINK_SCRIPT}")
target_include_directories(msp430-tests PRIVATE test) # book_test.h for the shared suite
test_chdir_to_bindir(msp430-tests)
gtest_discover_tests(msp430-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
