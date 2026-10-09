#
# Hosted Linux targets, x86_64-linux and aarch64-linux: programs assembled and linked by
# the system C compiler against glibc, which supplies the C library and the startup.
# Ours are the headers, glibc-compatible, and libvcc.a: the helpers our code generators
# call that glibc lacks (__va_arg) and the coroutine runtime (co, costack).  The data model and the code are those of the
# bare-metal x86_64 and aarch64; the code generator only adds .note.GNU-stack (--linux).
#
# The C compiler is the host's, VCC_HOST_CC (the one building vcc, but the system cc
# when that is vcc), when the host is that target, else <triple>-gcc on PATH.  It is <T>_LINUX_CC, cached for vcc and the tests.
#
set(LINUX_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE INTERNAL "")

# vcc_linux_target(<name> <VAR> <arch> <triple> <gen> <helper source dir>
#                  <processor regex> <model include dir>...)
function(vcc_linux_target name var arch triple gen helper_dir cpu_regex)
    set(arch_include ${CMAKE_CURRENT_SOURCE_DIR}/${arch}/include)
    # Search order, the first header of a name wins.
    set(dirs ${arch_include} ${LINUX_INCLUDE_DIR} ${ARGN} ${LIBC_COMMON_INCLUDE_DIR})
    set(${var}_LINUX_INCLUDE_DIRS ${dirs} CACHE INTERNAL "")

    # One directory of headers to install.
    set(headers "")
    set(seen "")
    foreach(dir ${dirs})
        file(GLOB files ${dir}/*.h)
        foreach(header ${files})
            get_filename_component(hname ${header} NAME)
            if(NOT hname IN_LIST seen)
                list(APPEND seen ${hname})
                list(APPEND headers ${header})
            endif()
        endforeach()
    endforeach()
    install(FILES ${headers} DESTINATION share/vcc/${name}/include)
    # The POSIX headers in subdirectories, enough for vcc to compile itself.
    install(DIRECTORY ${arch_include}/sys ${LINUX_INCLUDE_DIR}/sys
            DESTINATION share/vcc/${name}/include)

    # The headers parse, through the system preprocessor and through ours.
    add_test(NAME ${name}-headers
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
                ${SystemCpp} $<TARGET_FILE:parse>
                ${CMAKE_CURRENT_BINARY_DIR}/${name}-header-check ${dirs})
    add_test(NAME ${name}-headers-cpp
        COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-t${name}
                ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
                $<TARGET_FILE:cpp> $<TARGET_FILE:parse>
                ${CMAKE_CURRENT_BINARY_DIR}/${name}-header-check-cpp ${dirs})

    set(cc "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "${cpu_regex}")
        set(cc ${VCC_HOST_CC})
    else()
        find_program(${var}_LINUX_GCC NAMES ${triple}-gcc HINTS ${VCC_CROSS_HINTS})
        if(${var}_LINUX_GCC)
            set(cc ${${var}_LINUX_GCC})
        endif()
    endif()
    set(${var}_LINUX_CC "${cc}" CACHE INTERNAL "")
    if(NOT cc)
        message(STATUS "No C compiler for ${triple}: the ${name} runtime is not built")
        return()
    endif()

    set(out ${CMAKE_CURRENT_BINARY_DIR}/${name})
    file(MAKE_DIRECTORY ${out})
    set(${var}_LINUX_LIB_DIR ${out} CACHE INTERNAL "")
    set(includes "")
    set(deps "")
    foreach(dir ${dirs})
        list(APPEND includes -I${dir})
        file(GLOB files ${dir}/*.h)
        list(APPEND deps ${files})
    endforeach()
    set(objs "")
    foreach(src va_arg co costack)
        set(dir ${helper_dir})
        if(NOT src STREQUAL "va_arg")
            set(dir ${LIBC_COMMON_DIR})
        endif()
        add_custom_command(OUTPUT ${out}/${src}.o
            COMMAND ${SystemCpp} -E -nostdinc ${includes} ${dir}/${src}.c
                    -o ${out}/${src}.i
            COMMAND parse ${out}/${src}.i ${out}/${src}.ast
            COMMAND lower -t ${name} ${out}/${src}.ast ${out}/${src}.tac
            COMMAND ${gen} --linux ${out}/${src}.tac ${out}/${src}.s
            COMMAND ${cc} -c -o ${out}/${src}.o ${out}/${src}.s
            DEPENDS ${dir}/${src}.c ${deps} parse lower ${gen}
            COMMENT "Compiling ${src}.c -> ${src}.o (${name})")
        list(APPEND objs ${out}/${src}.o)
    endforeach()
    add_custom_command(OUTPUT ${out}/libvcc.a
        COMMAND ${CMAKE_COMMAND} -E rm -f ${out}/libvcc.a
        COMMAND ${CMAKE_AR} rcs ${out}/libvcc.a ${objs}
        DEPENDS ${objs})
    add_custom_target(${name}-libc ALL DEPENDS ${out}/libvcc.a)
    install(FILES ${out}/libvcc.a DESTINATION share/vcc/${name}/lib)
endfunction()

# x86-64's own float.h and limits.h come before the LP64 ones.
vcc_linux_target(x86_64-linux X86_64 x86_64 x86_64-linux-gnu genx86
    ${CMAKE_SOURCE_DIR}/libc/x86 "^(x86_64|AMD64|amd64)$"
    ${X86_INCLUDE_DIR} ${LP64_INCLUDE_DIR})
vcc_linux_target(aarch64-linux AARCH64 aarch64 aarch64-linux-gnu genaarch64
    ${CMAKE_SOURCE_DIR}/libc/aarch64 "^(aarch64|arm64|ARM64)$"
    ${AARCH64_INCLUDE_DIR} ${LP64_INCLUDE_DIR})
