add_library(optimize STATIC
    optimize.c
    const_fold.c
    cfg.c
    unreachable.c
    alias.c
    dataflow.c
    copy_prop.c
    cse.c
    ivsr.c
    dead_store.c
    liveness.c
)
target_include_directories(optimize PUBLIC .)
# const_fold.c queries the active Target (target_config) to wrap folded integer
# results to the target's signed/unsigned widths.  semantic provides target.{c,h}
# and does not depend on optimize, so this adds no link cycle.
target_link_libraries(optimize tac libutil semantic)

#
# Tests for optimizer (including the textbook chapter tests)
#
add_executable(optimizer-tests
    test/const_fold_tests.cpp
    test/type_conv_tests.cpp
    test/jump_unreachable_tests.cpp
    test/copy_prop_tests.cpp
    test/dead_store_tests.cpp
    test/cse_tests.cpp
    test/loop_tests.cpp
    test/pipeline_tests.cpp
    test/chapter19_tests1.cpp
    test/chapter19_tests2.cpp
    test/chapter19_tests3.cpp
    test/chapter19_tests4.cpp
)
target_link_libraries(optimizer-tests optimize parser translator tac GTest::gtest_main test_util)
target_compile_definitions(optimizer-tests PRIVATE
    TEST_CPP="${SystemCpp}"
    TEST_INCLUDE_DIR="${BESM6_INCLUDE_DIR}"
    TEST_COMMON_INCLUDE_DIR="${LIBC_COMMON_INCLUDE_DIR}")
test_chdir_to_bindir(optimizer-tests)
gtest_discover_tests(optimizer-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
