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
# ... and, in the same directory, those only for the byte-addressed IEEE-754 targets
# (RISC-V, AArch64): the __doprnt printf engine and the binary128 long double runtime.
set(LIBC_C_IEEE doprnt float128)
# C sources that assume LP64: a 64-bit long holds a double's bits (riscv64, aarch64).
set(LIBC_LP64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lp64)
set(LIBC_C_LP64 frexp ldexp modf)
# ... and those that assume ILP32: a double is two 32-bit words, and long long has
# its division and conversions in software (riscv32).
set(LIBC_ILP32_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ilp32)
set(LIBC_C_ILP32 frexp ldexp modf int64)

add_subdirectory(besm6)
# riscv32 shares sources with riscv64, and uses the tools riscv64 found.
set(RISCV64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/riscv64)
add_subdirectory(riscv64)
add_subdirectory(riscv32)
# aarch64 and arm32 use the tools riscv64 found.
add_subdirectory(aarch64)
add_subdirectory(arm32)
add_subdirectory(x86)
