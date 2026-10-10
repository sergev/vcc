#
# AST library
#
add_library(ast STATIC
    ast_alloc.c
    ast_print.c
    ast_free.c
    ast_clone.c
    ast_yaml.c
    ast_graphviz.c
    ast_export.c
    ast_import.c
    ast_compare.c
    type_to_c.c
)
target_include_directories(ast PUBLIC .)
target_link_libraries(ast libutil)

#
# Tests for AST
#
add_executable(ast-tests
    test/clone_tests.cpp
    test/type_to_c_tests.cpp
)
target_link_libraries(ast-tests ast libutil GTest::gtest_main)
test_chdir_to_bindir(ast-tests)
gtest_discover_tests(ast-tests EXTRA_ARGS --gtest_repeat=1 PROPERTIES TIMEOUT 10)
