#
# Code shared by every backend: the command-line driver.
#
add_library(backend_common STATIC
    driver.c
)
target_include_directories(backend_common PUBLIC .)
target_link_libraries(backend_common tac libutil)
