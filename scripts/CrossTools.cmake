#
# Cross tools of the qemu targets (riscv64, riscv32, aarch64, arm32, x86_64, avr), the
# MSP430 and wasm32: GNU binutils first, clang + ld.lld + llvm-ar as the fallback, and
# clang alone as the optional reference compiler of the tests.  wasm32 has no binutils:
# clang, wasm-ld and llvm-ar only.
#
# VCC_CROSS_TOOLS selects: auto (binutils, else clang), gnu or llvm.
#
set(VCC_CROSS_TOOLS auto CACHE STRING "Cross assembler/linker: auto, gnu or llvm")
set_property(CACHE VCC_CROSS_TOOLS PROPERTY STRINGS auto gnu llvm)

set(VCC_CROSS_HINTS $ENV{HOME}/.local/bin /opt/homebrew/bin /usr/local/bin)

# One clang, llvm-ar and ld.lld for every target, as versioned names too.
set(_clang_names clang)
set(_ar_names llvm-ar)
set(_llvm_hints /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
foreach(v RANGE 25 14 -1)
    list(APPEND _clang_names clang-${v})
    list(APPEND _ar_names llvm-ar-${v})
    list(APPEND _llvm_hints /usr/lib/llvm-${v}/bin)
endforeach()
find_program(VCC_CLANG NAMES ${_clang_names} HINTS ${_llvm_hints})
find_program(VCC_LLVM_AR NAMES ${_ar_names} HINTS ${_llvm_hints})
find_program(VCC_LLD NAMES ld.lld HINTS /opt/homebrew/bin ${_llvm_hints})
set(VCC_CLANG_TARGETS "")
if(VCC_CLANG)
    execute_process(COMMAND ${VCC_CLANG} --print-targets OUTPUT_VARIABLE VCC_CLANG_TARGETS
        ERROR_QUIET)
endif()

#
# vcc_find_cross(<VAR>
#     PREFIXES <prefix>...     binutils prefixes, tried in order; "-" is the host's own
#     GNU_ASFLAGS <flag>...    GNU as flags
#     GNU_LDFLAGS <flag>...    GNU ld flags
#     CLANG_TARGET <regex>     the target's line in clang --print-targets
#     CLANG_FLAGS <flag>...    clang --target=… and the ABI, for assembling and compiling
#     LLD_FLAGS <flag>...      ld.lld flags
#     LD <name>                the LLVM linker in place of ld.lld (wasm-ld)
#     [RWX_IMAGE]              the image is one RWX segment: GNU ld (2.39 and later) is
#                              told not to warn of it
#     [LLVM_ONLY])             no binutils exist for the target: clang whatever
#                              VCC_CROSS_TOOLS says
#
# Sets, cached:
#   <VAR>_TOOLS_FOUND   an assembler, a linker and an archiver
#   <VAR>_GNU           they are GNU binutils
#   <VAR>_AS            the assembler command with its flags, before -o <obj> <src>
#   <VAR>_LD            the linker, <VAR>_LDFLAGS its flags
#   <VAR>_AR            the archiver
#   <VAR>_CLANG_FOUND   clang has the target: <VAR>_CLANG, with <VAR>_TARGET_FLAGS
#   <VAR>_ASSEMBLER, <VAR>_LINK_FLAGS: <VAR>_AS and <VAR>_LDFLAGS as one blank-separated
#                       string, for a C define
#
function(vcc_find_cross var)
    cmake_parse_arguments(A "RWX_IMAGE;LLVM_ONLY" "CLANG_TARGET;LD"
        "PREFIXES;GNU_ASFLAGS;GNU_LDFLAGS;CLANG_FLAGS;LLD_FLAGS" ${ARGN})

    set(lld ${VCC_LLD})
    if(A_LD)
        unset(exe CACHE)
        find_program(exe NAMES ${A_LD} HINTS /opt/homebrew/bin ${_llvm_hints})
        set(lld ${exe})
        unset(exe CACHE)
    endif()

    set(as "")
    set(ld "")
    set(ar "")
    set(ldflags "")
    set(gnu OFF)
    if(NOT VCC_CROSS_TOOLS STREQUAL "llvm")
        foreach(p ${A_PREFIXES})
            if(p STREQUAL "-")
                set(pre "")
            else()
                set(pre "${p}-")
            endif()
            foreach(tool as ld ar)
                unset(exe CACHE)
                find_program(exe NAMES ${pre}${tool} HINTS ${VCC_CROSS_HINTS})
                set(${tool}_exe ${exe})
                unset(exe CACHE)
            endforeach()
            if(as_exe AND ld_exe AND ar_exe)
                set(as ${as_exe} ${A_GNU_ASFLAGS})
                set(ld ${ld_exe})
                set(ar ${ar_exe})
                set(ldflags ${A_GNU_LDFLAGS})
                if(A_RWX_IMAGE)
                    execute_process(COMMAND ${ld_exe} --help OUTPUT_VARIABLE help ERROR_QUIET)
                    if(help MATCHES "--no-warn-rwx-segments")
                        list(APPEND ldflags --no-warn-rwx-segments)
                    endif()
                endif()
                set(gnu ON)
                break()
            endif()
        endforeach()
    endif()

    set(clang_found OFF)
    if(VCC_CLANG AND VCC_CLANG_TARGETS MATCHES "${A_CLANG_TARGET}")
        set(clang_found ON)
    endif()
    if(NOT gnu AND (A_LLVM_ONLY OR NOT VCC_CROSS_TOOLS STREQUAL "gnu") AND clang_found
       AND lld AND VCC_LLVM_AR)
        set(as ${VCC_CLANG} ${A_CLANG_FLAGS} -c)
        set(ld ${lld})
        set(ar ${VCC_LLVM_AR})
        set(ldflags ${A_LLD_FLAGS})
    endif()

    set(found OFF)
    if(as)
        set(found ON)
        list(GET as 0 tool)
        message(STATUS "${var}: assembler ${tool}, linker ${ld}")
    else()
        string(JOIN ", " prefixes ${A_PREFIXES})
        if(A_LD)
            set(lld_name ${A_LD})
        else()
            set(lld_name ld.lld)
        endif()
        message(STATUS "${var}: no binutils (${prefixes}) and no clang/${lld_name}/llvm-ar")
    endif()
    string(JOIN " " assembler ${as})
    string(JOIN " " link_flags ${ldflags})
    set(${var}_TOOLS_FOUND ${found} CACHE INTERNAL "")
    set(${var}_GNU ${gnu} CACHE INTERNAL "")
    set(${var}_AS "${as}" CACHE INTERNAL "")
    set(${var}_LD "${ld}" CACHE INTERNAL "")
    set(${var}_LDFLAGS "${ldflags}" CACHE INTERNAL "")
    set(${var}_AR "${ar}" CACHE INTERNAL "")
    set(${var}_ASSEMBLER "${assembler}" CACHE INTERNAL "")
    set(${var}_LINK_FLAGS "${link_flags}" CACHE INTERNAL "")
    set(${var}_CLANG_FOUND ${clang_found} CACHE INTERNAL "")
    set(${var}_CLANG "${VCC_CLANG}" CACHE INTERNAL "")
    set(${var}_TARGET_FLAGS "${A_CLANG_FLAGS}" CACHE INTERNAL "")
endfunction()

#
# vcc_assemble_crt0(<VAR> <src dir> <out dir>): crt0.o and crt0-status.o (PRINT_STATUS)
# from crt0.S, preprocessed by the C compiler and assembled by <VAR>_AS.
#
function(vcc_assemble_crt0 var src out)
    foreach(obj crt0 crt0-status)
        if(obj STREQUAL "crt0-status")
            set(defs -DPRINT_STATUS)
        else()
            set(defs "")
        endif()
        add_custom_command(OUTPUT ${out}/${obj}.o
            COMMAND ${SystemCpp} -E -P -x assembler-with-cpp ${defs} ${src}/crt0.S
                    -o ${out}/${obj}.S.s
            COMMAND ${${var}_AS} -o ${out}/${obj}.o ${out}/${obj}.S.s
            DEPENDS ${src}/crt0.S)
    endforeach()
endfunction()
