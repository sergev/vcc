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

# wasm32-braam (docs/Braam.md §7): its own headers first, then wasm32's, the first
# of a name winning.  Staged in the build tree as an installation lays them out, so the
# driver finds them from build/cc (the Braam tests run it).
set(BRAAM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/braam/include)
set(BRAAM_SHARE ${CMAKE_BINARY_DIR}/share/vcc/wasm32-braam)
file(GLOB BRAAM_OWN_HEADERS ${BRAAM_INCLUDE_DIR}/*.h)
file(GLOB BRAAM_SYS_HEADERS ${BRAAM_INCLUDE_DIR}/sys/*.h)
set(BRAAM_TARGET_HEADERS ${BRAAM_OWN_HEADERS})
foreach(h ${WASM32_TARGET_HEADERS} ${WASM32_COMMON_HEADERS})
    get_filename_component(name ${h} NAME)
    if(NOT EXISTS ${BRAAM_INCLUDE_DIR}/${name})
        list(APPEND BRAAM_TARGET_HEADERS ${h})
    endif()
endforeach()
install(FILES ${BRAAM_TARGET_HEADERS} DESTINATION share/vcc/wasm32-braam/include)
install(FILES ${BRAAM_SYS_HEADERS} DESTINATION share/vcc/wasm32-braam/include/sys)
file(MAKE_DIRECTORY ${BRAAM_SHARE}/include/sys ${BRAAM_SHARE}/lib)
foreach(h ${BRAAM_TARGET_HEADERS})
    get_filename_component(name ${h} NAME)
    configure_file(${h} ${BRAAM_SHARE}/include/${name} COPYONLY)
endforeach()
foreach(h ${BRAAM_SYS_HEADERS})
    get_filename_component(name ${h} NAME)
    configure_file(${h} ${BRAAM_SHARE}/include/sys/${name} COPYONLY)
endforeach()
list(APPEND BRAAM_TARGET_HEADERS ${BRAAM_SYS_HEADERS})

add_test(
    NAME braam-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-twasm32-braam
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp>
            $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-braam
            ${BRAAM_SHARE}/include)

# The ABI numbers of braam.h against braam-core's src/kernel/sysabi.h, when that tree is
# beside this one (or named by BRAAM_CORE): a drift is a failing test, not a stale binary.
set(BRAAM_CORE ${CMAKE_SOURCE_DIR}/../../Braam/braam-core CACHE PATH "a braam-core checkout")
if(EXISTS ${BRAAM_CORE}/src/kernel/sysabi.h)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME braam-abi
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_braam_abi.py
                    ${BRAAM_INCLUDE_DIR}/braam.h ${BRAAM_CORE}/src/kernel/sysabi.h
                    ${BRAAM_CORE}/src/kernel/result.h ${BRAAM_INCLUDE_DIR}/errno.h)
    endif()
endif()

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
foreach(name console main memory sqrt)
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

# The common sources but for those the bulk memory instructions replace (memory.s), and
# the IEEE ones: the printf engine and the binary128 long double runtime.
set(WASM32_LIBC_C ${LIBC_C_COMMON} ${LIBC_C_IEEE})
list(REMOVE_ITEM WASM32_LIBC_C memcpy memmove memset)
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

# The Braam runtime: crt0.o is the exports (braam/exports.s); libc.a is wasm32's but for
# the console, main.s, the bump allocator and strerror, with the process runtime, the
# system calls and the streams of braam/, compiled with Braam's headers.
set(bsrc ${src}/braam)
set(bout ${out}/braam)
file(MAKE_DIRECTORY ${bout})
add_custom_command(OUTPUT ${bout}/crt0.o
    COMMAND ${WASM32_AS} -o ${bout}/crt0.o ${bsrc}/exports.s
    DEPENDS ${bsrc}/exports.s)
set(BRAAM_LIBC_OBJS "")
foreach(o ${WASM32_LIBC_OBJS})
    get_filename_component(name ${o} NAME_WE)
    if(NOT name MATCHES "^(console|main|malloc|strerror)$")
        list(APPEND BRAAM_LIBC_OBJS ${o})
    endif()
endforeach()
foreach(name rt sys stdio malloc strerror taskbytes)
    add_custom_command(OUTPUT ${bout}/${name}.o
        COMMAND cpp -t wasm32-braam -nostdinc -I${BRAAM_SHARE}/include
                ${bsrc}/${name}.c ${bout}/${name}.i
        COMMAND parse ${bout}/${name}.i ${bout}/${name}.ast
        COMMAND lower -t wasm32-braam ${bout}/${name}.ast ${bout}/${name}.tac
        COMMAND genwasm ${bout}/${name}.tac ${bout}/${name}.s
        COMMAND ${WASM32_AS} -o ${bout}/${name}.o ${bout}/${name}.s
        DEPENDS ${bsrc}/${name}.c ${BRAAM_TARGET_HEADERS} cpp parse lower genwasm
        COMMENT "Compiling ${name}.c -> ${name}.o (wasm32-braam)")
    list(APPEND BRAAM_LIBC_OBJS ${bout}/${name}.o)
endforeach()
add_custom_command(OUTPUT ${bout}/libc.a
    COMMAND ${CMAKE_COMMAND} -E rm -f ${bout}/libc.a
    COMMAND ${WASM32_AR} rcs ${bout}/libc.a ${BRAAM_LIBC_OBJS}
    DEPENDS ${BRAAM_LIBC_OBJS})
add_custom_command(OUTPUT ${BRAAM_SHARE}/lib/crt0.o ${BRAAM_SHARE}/lib/libc.a
                          ${BRAAM_SHARE}/lib/run.mjs
    COMMAND ${CMAKE_COMMAND} -E copy ${bout}/crt0.o ${bout}/libc.a ${bsrc}/run.mjs
            ${BRAAM_SHARE}/lib
    DEPENDS ${bout}/crt0.o ${bout}/libc.a ${bsrc}/run.mjs)
add_custom_target(braam-libc ALL
    DEPENDS ${BRAAM_SHARE}/lib/crt0.o ${BRAAM_SHARE}/lib/libc.a ${BRAAM_SHARE}/lib/run.mjs)
# The objects shared with wasm32's libc.a are built by wasm32-libc alone: a Makefile
# generator gives each target its own copy of their rules, which would race in parallel.
add_dependencies(braam-libc wasm32-libc)
install(FILES ${bout}/crt0.o ${bout}/libc.a ${bsrc}/run.mjs
        DESTINATION share/vcc/wasm32-braam/lib)

# On Braam itself (docs/Braam.md §7.5): when BRAAM_CORE is a built checkout,
# hello, cat, wc and docs/examples/notes.c built by the in-tree driver and run from
# the shell of a session of its system harness, after its abi.mjs checks them.
if(WASM32_NODE AND EXISTS ${BRAAM_CORE}/build/kernel.wasm
   AND EXISTS ${BRAAM_CORE}/build/web/rootfs.zip
   AND EXISTS ${BRAAM_CORE}/test/system/harness.mjs)
    add_test(NAME braam-system
        COMMAND ${CMAKE_COMMAND} -E env
                VCC_CPP=$<TARGET_FILE:cpp> VCC_PARSE=$<TARGET_FILE:parse>
                VCC_LOWER=$<TARGET_FILE:lower> VCC_GEN=$<TARGET_FILE:genwasm>
                ${WASM32_NODE} ${CMAKE_SOURCE_DIR}/backend/wasm/test/braam_system.mjs
                ${BRAAM_CORE} ${CMAKE_CURRENT_BINARY_DIR}/braam-system
                $<TARGET_FILE:cc> ${CMAKE_SOURCE_DIR}/docs/examples)
endif()
