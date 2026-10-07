#
# wasm32 runtime (see docs/Wasm_Backend.md): crt0.o, crt0-status.o (prints main's
# result, for the book run tests) and libc.a, for programs that run under node with
# run.mjs as the host.  clang assembles, wasm-ld links and llvm-ar archives: there are
# no binutils for wasm.
#
find_program(WASM32_NODE NAMES node HINTS ${VCC_CROSS_HINTS})

# Braam's wasm features (braam-core's cmake/wasm32-unknown-unknown.cmake), on clang's
# default CPU: the assembler, vcc and every reference build by clang take the same list,
# so all objects agree on their target_features and wasm-ld links them together.
set(WASM32_FEATURES -mreference-types -mbulk-memory -msign-ext -mmutable-globals
    -mnontrapping-fptoint CACHE INTERNAL "")

vcc_find_cross(WASM32
    CLANG_TARGET "[\n] *wasm32 +- "
    CLANG_FLAGS --target=wasm32 --no-default-config ${WASM32_FEATURES}
    LD wasm-ld
    LLD_FLAGS --stack-first -z stack-size=1048576
    LLVM_ONLY)
set(WASM32_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(WASM32_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run.mjs CACHE INTERNAL "")

# Standard-library header check, as for riscv32 ...
add_test(
    NAME wasm32-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp}
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check
            ${WASM32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# ... and the same through our own preprocessor.
add_test(
    NAME wasm32-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-twasm32
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp
            ${WASM32_INCLUDE_DIR}
            ${ILP32_INCLUDE_DIR}
            ${LIBC_COMMON_INCLUDE_DIR})

# All the headers go into one directory, the target's own first: ILP32's limits.h
# assumes an unsigned char.
file(GLOB WASM32_OWN_HEADERS ${WASM32_INCLUDE_DIR}/*.h)
set(WASM32_TARGET_HEADERS ${WASM32_OWN_HEADERS})
file(GLOB WASM32_ILP32_HEADERS ${ILP32_INCLUDE_DIR}/*.h)
foreach(h ${WASM32_ILP32_HEADERS})
    get_filename_component(name ${h} NAME)
    if(NOT EXISTS ${WASM32_INCLUDE_DIR}/${name})
        list(APPEND WASM32_TARGET_HEADERS ${h})
    endif()
endforeach()
file(GLOB WASM32_COMMON_HEADERS ${LIBC_COMMON_INCLUDE_DIR}/*.h)
install(FILES ${WASM32_TARGET_HEADERS} ${WASM32_COMMON_HEADERS}
        DESTINATION share/vcc/wasm32/include)

if(NOT WASM32_TOOLS_FOUND)
    message(STATUS "No clang with wasm32, wasm-ld or llvm-ar: the wasm32 runtime is not built")
    return()
endif()
if(NOT WASM32_NODE)
    message(STATUS "No node: the wasm32 run tests will be skipped")
endif()

set(src ${CMAKE_CURRENT_SOURCE_DIR})
set(out ${CMAKE_CURRENT_BINARY_DIR})

vcc_assemble_crt0(WASM32 ${src} ${out})
# Assembly leaves.
set(WASM32_LIBC_OBJS "")
foreach(name console main memory)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${WASM32_AS} -o ${out}/${name}.o ${src}/${name}.s
        DEPENDS ${src}/${name}.s)
    list(APPEND WASM32_LIBC_OBJS ${out}/${name}.o)
endforeach()

# C sources, through cpp -> parse -> lower -> genwasm -> clang.
file(GLOB WASM32_LIBC_HEADERS ${WASM32_INCLUDE_DIR}/*.h ${ILP32_INCLUDE_DIR}/*.h
     ${LIBC_COMMON_INCLUDE_DIR}/*.h)
macro(wasm32_compile_libc_c name srcdir)
    add_custom_command(OUTPUT ${out}/${name}.o
        COMMAND ${SystemCpp} -E -nostdinc -I${WASM32_INCLUDE_DIR} -I${ILP32_INCLUDE_DIR}
                -I${LIBC_COMMON_INCLUDE_DIR}
                ${srcdir}/${name}.c -o ${out}/${name}.i
        COMMAND parse ${out}/${name}.i ${out}/${name}.ast
        COMMAND lower -t wasm32 ${out}/${name}.ast ${out}/${name}.tac
        COMMAND genwasm ${out}/${name}.tac ${out}/${name}.s
        COMMAND ${WASM32_AS} -o ${out}/${name}.o ${out}/${name}.s
        DEPENDS ${srcdir}/${name}.c ${WASM32_LIBC_HEADERS} parse lower genwasm
        COMMENT "Compiling ${name}.c -> ${name}.o (wasm32)")
    list(APPEND WASM32_LIBC_OBJS ${out}/${name}.o)
endmacro()

# The common sources but for those the bulk memory instructions replace (memory.s) and
# the printf family, which needs variable arguments, to come.
set(WASM32_LIBC_C ${LIBC_C_COMMON})
list(REMOVE_ITEM WASM32_LIBC_C memcpy memmove memset printf snprintf
     sprintf)
foreach(name ${WASM32_LIBC_C})
    wasm32_compile_libc_c(${name} ${LIBC_COMMON_DIR})
endforeach()
# The ILP32 sources but int64.c and int64conv.c: long long is native here.
foreach(name frexp ldexp modf)
    wasm32_compile_libc_c(${name} ${LIBC_ILP32_DIR})
endforeach()
wasm32_compile_libc_c(malloc ${src})

add_custom_command(OUTPUT ${out}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libc.a
    COMMAND ${WASM32_AR} rcs ${out}/libc.a ${WASM32_LIBC_OBJS}
    DEPENDS ${WASM32_LIBC_OBJS})
add_custom_target(wasm32-libc ALL
    DEPENDS ${out}/crt0.o ${out}/crt0-status.o ${out}/libc.a)

# The runtime, with the host that runs a program under node.
install(FILES ${out}/crt0.o ${out}/libc.a ${src}/run.mjs DESTINATION share/vcc/wasm32/lib)
