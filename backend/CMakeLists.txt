add_subdirectory(common)
add_subdirectory(besm6)

#
# `genbesm` executable
#
add_executable(genbesm
    besm6/main.c
)
target_link_libraries(genbesm besm backend_common)

install(PROGRAMS $<TARGET_FILE:genbesm> DESTINATION bin RENAME b6codegen)
