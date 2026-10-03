add_subdirectory(common)
add_subdirectory(besm6)
add_subdirectory(riscv)
add_subdirectory(aarch64)

#
# `genbesm` executable
#
add_executable(genbesm
    besm6/main.c
)
target_link_libraries(genbesm besm backend_common)

install(PROGRAMS $<TARGET_FILE:genbesm> DESTINATION bin RENAME vgenbesm6)

#
# `genriscv` executable
#
add_executable(genriscv
    riscv/main.c
)
target_link_libraries(genriscv riscv backend_common)

install(PROGRAMS $<TARGET_FILE:genriscv> DESTINATION bin RENAME vgenriscv64)
install(PROGRAMS $<TARGET_FILE:genriscv> DESTINATION bin RENAME vgenriscv32)

#
# `genaarch64` executable
#
add_executable(genaarch64
    aarch64/main.c
)
target_link_libraries(genaarch64 aarch64 backend_common)

install(PROGRAMS $<TARGET_FILE:genaarch64> DESTINATION bin RENAME vgenaarch64)
