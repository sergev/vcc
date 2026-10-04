#
# AVR runtime for bare-metal qemu `arduino-mega` (the ATmega1280; see
# backend/avr/Plan.md).  Uses the clang, llvm-ar and ld.lld libc/riscv64 found, when
# that clang has the AVR target; qemu-system-avr runs the programs.
#
find_program(AVR_QEMU NAMES qemu-system-avr)

set(AVR_TOOLS_FOUND OFF)
if(RISCV_CLANG AND RISCV_AR AND RISCV_LD)
    execute_process(COMMAND ${RISCV_CLANG} --print-targets OUTPUT_VARIABLE targets
        ERROR_QUIET)
    if(targets MATCHES "[\n] *avr +- ")
        set(AVR_TOOLS_FOUND ON)
    endif()
endif()
# For the backend tests.
set(AVR_TOOLS_FOUND ${AVR_TOOLS_FOUND} CACHE INTERNAL "")
set(AVR_CLANG ${RISCV_CLANG} CACHE INTERNAL "")
set(AVR_AR ${RISCV_AR} CACHE INTERNAL "")
set(AVR_LD ${RISCV_LD} CACHE INTERNAL "")
set(AVR_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(AVR_LINK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/link.ld CACHE INTERNAL "")
# The ATmega1280: avr51, 128 KB of flash, a 2-byte PC.
set(AVR_TARGET_FLAGS --target=avr -mmcu=atmega1280 CACHE INTERNAL "")

if(NOT AVR_TOOLS_FOUND)
    message(STATUS "No AVR clang/llvm-ar/ld.lld: the AVR runtime is not built")
    return()
endif()
if(NOT AVR_QEMU)
    message(STATUS "No qemu-system-avr: the AVR run tests will be skipped")
endif()
