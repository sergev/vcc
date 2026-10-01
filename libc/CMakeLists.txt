#
# Target C runtime libraries.  Each target/assembler combination lives in its own
# subdirectory (e.g. besm6/madlen; besm6/unix and besm6/bemsh planned) and builds
# an assembled runtime that programs compiled by this toolchain link against.
#
add_subdirectory(besm6)
add_subdirectory(riscv)
