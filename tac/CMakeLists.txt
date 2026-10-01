#
# TAC library
#
add_library(tac STATIC
    tac_alloc.c
    tac_print.c
    tac_free.c
    tac_compare.c
    tac_yaml.c
    tac_import.c
    tac_export.c
    tac_graphviz.c
    tac_walk.c
)
target_include_directories(tac PUBLIC .)
target_link_libraries(tac libutil)

#
# Tests for tac binary export/import, tac_yaml and tac_graphviz
#
add_executable(tac-tests
    test/binary_tests.cpp
    test/yaml_tests.cpp
    test/graphviz_tests.cpp
)
target_link_libraries(tac-tests tac GTest::gtest_main)
test_chdir_to_bindir(tac-tests)
gtest_discover_tests(tac-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
