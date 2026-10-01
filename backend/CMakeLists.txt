add_subdirectory(common)
add_subdirectory(besm6)
add_subdirectory(riscv)

#
# `genbesm` executable
#
add_executable(genbesm
    besm6/main.c
)
target_link_libraries(genbesm besm backend_common)

install(PROGRAMS $<TARGET_FILE:genbesm> DESTINATION bin RENAME b6codegen)

#
# `genriscv` executable
#
add_executable(genriscv
    riscv/main.c
)
target_link_libraries(genriscv riscv backend_common)
