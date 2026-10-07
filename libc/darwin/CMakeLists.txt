#
# The hosted macOS target aarch64-darwin: programs assembled and linked by the system C
# compiler against libSystem, which supplies the C library and the startup.  Ours are
# the headers, libSystem-compatible; no library, since Apple's va_list is a pointer
# walked in the header and long double is double.  The code generator is genaarch64
# --darwin: Mach-O, the GOT and Apple's arm64 calling convention.
#
# The C compiler is the host's, VCC_HOST_CC (the one building vcc, but the system cc
# when that is vcc), when the host is macOS on Apple silicon.  It is AARCH64_DARWIN_CC,
# cached for vcc and the tests.
#
set(DARWIN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE INTERNAL "")

# Search order, the first header of a name wins.
set(dirs ${DARWIN_INCLUDE_DIR} ${AARCH64_INCLUDE_DIR} ${LP64_INCLUDE_DIR}
    ${LIBC_COMMON_INCLUDE_DIR})
set(AARCH64_DARWIN_INCLUDE_DIRS ${dirs} CACHE INTERNAL "")

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
install(FILES ${headers} DESTINATION share/vcc/aarch64-darwin/include)
# The POSIX headers in subdirectories, enough for vcc to compile itself.
install(DIRECTORY ${DARWIN_INCLUDE_DIR}/sys ${DARWIN_INCLUDE_DIR}/mach-o
        DESTINATION share/vcc/aarch64-darwin/include)

# The headers parse, through the system preprocessor and through ours.
add_test(NAME aarch64-darwin-headers
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            ${SystemCpp} $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check ${dirs})
add_test(NAME aarch64-darwin-headers-cpp
    COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-taarch64-darwin
            ${CMAKE_SOURCE_DIR}/scripts/check_headers.sh
            $<TARGET_FILE:cpp> $<TARGET_FILE:parse>
            ${CMAKE_CURRENT_BINARY_DIR}/header-check-cpp ${dirs})

set(cc "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(cc ${VCC_HOST_CC})
endif()
set(AARCH64_DARWIN_CC "${cc}" CACHE INTERNAL "")
