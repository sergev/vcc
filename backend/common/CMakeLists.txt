#
# Code shared by every backend: the command-line driver.
#
add_library(backend_common STATIC
    driver.c
)
target_include_directories(backend_common PUBLIC .)
target_link_libraries(backend_common tac libutil)

# Header-only test fixture shared by the backend test executables (backend_test.h).
add_library(backend_test_util INTERFACE)
target_include_directories(backend_test_util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/test)
