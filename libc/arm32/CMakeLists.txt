#
# ARM32 runtime for bare-metal qemu `virt` (see backend/arm32/Plan.md).  For now only
# the tools: the clang, llvm-ar and ld.lld libc/riscv64 found, when that clang has the
# 32-bit ARM target, and qemu-system-arm.
#
find_program(ARM32_QEMU NAMES qemu-system-arm)

set(ARM32_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    # The `arm` line itself, not `arm64` or `armeb`.
    if(targets MATCHES "[\n] *arm +- ")
        set(ARM32_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(ARM32_TOOLS_FOUND ${ARM32_TOOLS_FOUND} CACHE INTERNAL "")
set(ARM32_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(ARM32_LD ${RISCV_LD} CACHE INTERNAL "")
set(ARM32_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(ARM32_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")

if(NOT ARM32_TOOLS_FOUND)
    message(STATUS "No ARM32 clang/llvm-ar: the ARM32 runtime is not built")
    return()
endif()
if(NOT ARM32_QEMU)
    message(STATUS "No qemu-system-arm: the ARM32 run tests will be skipped")
endif()
