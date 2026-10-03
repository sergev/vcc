#
# x86-64 runtime for bare-metal qemu `microvm` (see backend/x86/Plan.md).  For now only
# the tools: the clang, llvm-ar and ld.lld libc/riscv64 found, when that clang has the
# x86-64 target; qemu-system-x86_64 runs the programs; GNU `as` from x86_64-elf-binutils,
# when installed, is a second assembler the tests check our output with.
#
find_program(X86_QEMU NAMES qemu-system-x86_64)
find_program(X86_GNU_AS NAMES x86_64-elf-as)

set(X86_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    # The `x86-64` line itself, not the 32-bit `x86`.
    if(targets MATCHES "[\n] *x86-64 +- ")
        set(X86_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(X86_TOOLS_FOUND ${X86_TOOLS_FOUND} CACHE INTERNAL "")
set(X86_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(X86_LD ${RISCV_LD} CACHE INTERNAL "")
set(X86_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(X86_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# Baseline x86-64 (SSE2), System V psABI, ELF, small code model.
set(X86_TARGET_FLAGS --target=x86_64-none-elf CACHE INTERNAL "")
if(NOT X86_TOOLS_FOUND)
    message(STATUS "No x86-64 clang/llvm-ar: the x86-64 runtime is not built")
elseif(NOT X86_QEMU)
    message(STATUS "No qemu-system-x86_64: the x86-64 run tests will be skipped")
endif()
