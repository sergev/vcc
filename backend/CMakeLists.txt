add_subdirectory(common)
add_subdirectory(besm6)
add_subdirectory(riscv)
add_subdirectory(aarch64)
add_subdirectory(arm32)
add_subdirectory(x86)
add_subdirectory(avr)
add_subdirectory(msp430)
add_subdirectory(mmix)
add_subdirectory(wasm)

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

#
# `genarm32` executable
#
add_executable(genarm32
    arm32/main.c
)
target_link_libraries(genarm32 arm32 backend_common)

install(PROGRAMS $<TARGET_FILE:genarm32> DESTINATION bin RENAME vgenarm32)

#
# `genx86` executable
#
add_executable(genx86
    x86/main.c
)
target_link_libraries(genx86 x86 backend_common)

install(PROGRAMS $<TARGET_FILE:genx86> DESTINATION bin RENAME vgenx86)

#
# `genavr` executable
#
add_executable(genavr
    avr/main.c
)
target_link_libraries(genavr avr backend_common)

install(PROGRAMS $<TARGET_FILE:genavr> DESTINATION bin RENAME vgenavr)

#
# `genmsp430` executable
#
add_executable(genmsp430
    msp430/main.c
)
target_link_libraries(genmsp430 msp430 backend_common)

install(PROGRAMS $<TARGET_FILE:genmsp430> DESTINATION bin RENAME vgenmsp430)

#
# `genmmix` executable
#
add_executable(genmmix
    mmix/main.c
)
target_link_libraries(genmmix mmix backend_common)

install(PROGRAMS $<TARGET_FILE:genmmix> DESTINATION bin RENAME vgenmmix)

#
# `genwasm` executable
#
add_executable(genwasm
    wasm/main.c
)
target_link_libraries(genwasm wasm backend_common)

install(PROGRAMS $<TARGET_FILE:genwasm> DESTINATION bin RENAME vgenwasm)
