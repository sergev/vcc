#
# Target C runtime libraries.  Each target/assembler combination lives in its own
# subdirectory (e.g. besm6/madlen; besm6/unix and besm6/bemsh planned) and builds
# an assembled runtime that programs compiled by this toolchain link against.
#

# Target-neutral C sources in libc/common, compiled by our own toolchain for each
# target.  printf/sprintf/snprintf wrap a target's own __doprnt.
set(LIBC_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set(LIBC_C_COMMON
    atoi fabs fma fmax fmin
    memchr memcmp memcpy memmove memset
    printf puts putchar snprintf sprintf
    strcat strchr strcmp strcpy strerror strlen strncat strncmp strncpy
    strrchr strstr strtok)

add_subdirectory(besm6)
# riscv32 shares sources with riscv64, and uses the tools riscv64 found.
set(RISCV64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv64)
add_subdirectory(riscv64)
add_subdirectory(riscv32)
