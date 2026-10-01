cmake_minimum_required(VERSION 3.10)
project(c-scanner)

# The new Apple/LLD linkers resolve symbols from all archives in one pass, so
# repeating static libraries (as a traditional linker needs) makes them warn:
#   ld: warning: ignoring duplicate libraries: '.../libsemantic.a'
# Enable CMake's linker-capability-based de-duplication to drop the repeats.
# (Both policies are OLD by default because cmake_minimum_required is 3.10.)
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Werror -Wshadow")
set(CMAKE_C_FLAGS   "-Wall -Werror -Wshadow")

#
# Enable cppcheck by default
#
find_program(Cppcheck NAMES cppcheck)
if(NOT (Cppcheck MATCHES "NOTFOUND"))
    message(STATUS "Found Cppcheck: ${Cppcheck}")
    set(CMAKE_CXX_CPPCHECK "${Cppcheck}"
        "--std=c++17"
        "--enable=style"
        "--check-level=exhaustive"
        "--error-exitcode=1" # Fail on any issues
        "--inline-suppr" # Enable inline control like // cppcheck-suppress "id"
        "--quiet" # No progress report messages
        "--suppress=badBitmaskCheck" # Allow redundant zero operands
        "--suppress=uninitMemberVarNoCtor" # Allow member variables without initializer
        "--suppress=*:*/_deps/*" # Ignore issues in Googletest
        "--library=${CMAKE_SOURCE_DIR}/scripts/googletest.xml" # Parse TEST() macro properly
    )
    set(CMAKE_C_CPPCHECK "${Cppcheck}"
        "--std=c11"
        "--enable=style"
        "--check-level=exhaustive"
        "--error-exitcode=1" # Fail on any issues
        "--inline-suppr" # Enable inline control like // cppcheck-suppress "id"
        "--quiet" # No progress report messages
    )
endif()

#
# C preprocessor used by the BESM-6 libc build, the besm-headers test, and the
# test fixtures (which run their C snippets through it so tests can #include the
# shipped standard headers).  We use the C compiler's own preprocessor via -E
# rather than a standalone `cpp`: a traditional cpp (e.g. Apple's /usr/bin/cpp)
# only recognises a '#' directive in column 1, whereas `cc -E` accepts directives
# anywhere on the line.  Exposed to all subdirectories as SystemCpp (always
# invoked with -E).  A target's headers are its own directory (BESM6_INCLUDE_DIR,
# RISCV_INCLUDE_DIR) followed by the target-neutral LIBC_COMMON_INCLUDE_DIR.
#
set(SystemCpp ${CMAKE_C_COMPILER})
set(BESM6_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/besm6/include)
set(RISCV_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/riscv/include)
set(LIBC_COMMON_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/common/include)

# Download GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    EXCLUDE_FROM_ALL
)
set(BUILD_GMOCK OFF CACHE BOOL "Builds the googlemock subproject" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Enable installation of googletest" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()

# Compile the chdir-to-build-dir environment into a test target and tell it where that
# dir is.  Every test binary then chdir()s into its own build directory at startup, so a
# manual run from another directory doesn't litter the source tree (ctest already runs
# them there).  CMAKE_CURRENT_BINARY_DIR reflects the *calling* directory (functions don't
# rescope directory vars), so each target gets its own build dir.
function(test_chdir_to_bindir target)
    target_sources(${target} PRIVATE ${CMAKE_SOURCE_DIR}/libutil/test/test_chdir.cpp)
    target_compile_definitions(${target} PRIVATE
        TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

add_subdirectory(libutil)
add_subdirectory(ast)
add_subdirectory(scanner)
add_subdirectory(parser)
add_subdirectory(tac)
add_subdirectory(semantic)
add_subdirectory(translator)
add_subdirectory(optimize)
# libc (the target runtime) is processed before backend so its `besm-libc-dubna` target
# exists when backend/besm6's besm-tests declares add_dependencies(besm-tests besm-libc-dubna).
# libc's compile commands DEPENDS on parse/lower/genbesm resolve at generate time, so
# referencing backend-defined `genbesm` from here is fine even though backend follows.
add_subdirectory(libc)
add_subdirectory(backend)

#
# 'parse' executable
#
add_executable(parse
    parser/main.c
)
target_link_libraries(parse parser scanner)

#
# 'lower' executable
#
add_executable(lower
    translator/main.c
)
target_link_libraries(lower optimize translator)

#
# Installation: compiler driver executables (renamed with a b6 prefix) and the
# BESM-6 C runtime library.  The Makefile `install` target picks the prefix
# (~/.local if present, else /usr/local) and passes it via `cmake --install --prefix`.
#
install(PROGRAMS $<TARGET_FILE:parse> DESTINATION bin RENAME b6parse)
install(PROGRAMS $<TARGET_FILE:lower> DESTINATION bin RENAME b6lower)
