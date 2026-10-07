cmake_minimum_required(VERSION 3.10)
project(c-scanner)

# Install into the user's ~/.local unless a prefix is given explicitly.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

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
# The sources are C11: a newer compiler's default (C23 in GCC 15) makes `alignas` and
# the like keywords.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# GCC 16 takes an unbraced `if` around a GoogleTest EXPECT_* for a dangling else, and
# the destructor of a vector<string> inlined into a constant-false branch (ClangRunBook,
# with no clang for the target) for a bad free.
set(CMAKE_CXX_FLAGS "-Wall -Werror -Wshadow -Wno-dangling-else -Wno-free-nonheap-object")
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
        "--library=${CMAKE_SOURCE_DIR}/scripts/cppcheck-c11.xml" # _Noreturn as GCC's noreturn
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
# RISCV_INCLUDE_DIR, RISCV32_INCLUDE_DIR, AARCH64_INCLUDE_DIR, ARM32_INCLUDE_DIR, X86_INCLUDE_DIR, AVR_INCLUDE_DIR,
# MSP430_INCLUDE_DIR, MMIX_INCLUDE_DIR), then the data-model ones (LP64_INCLUDE_DIR for riscv64, aarch64, x86_64 and mmix,
# ILP32_INCLUDE_DIR for riscv32 and arm32, IP16_INCLUDE_DIR for avr and msp430), and last the target-neutral
# LIBC_COMMON_INCLUDE_DIR.
#
set(SystemCpp ${CMAKE_C_COMPILER})

#
# The host's C compiler, which assembles and links for the hosted targets (and is baked
# into vcc for them): the one building vcc -- unless that is vcc itself (`make self`,
# an unknown compiler to CMake), whose own path must not end up in the driver it builds.
#
if(CMAKE_C_COMPILER_ID)
    set(VCC_HOST_CC ${CMAKE_C_COMPILER})
else()
    find_program(VCC_HOST_CC NAMES cc gcc clang)
endif()
set(BESM6_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/besm6/include)
set(RISCV_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/riscv64/include)
set(RISCV32_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/riscv32/include)
set(AARCH64_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/aarch64/include)
set(ARM32_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/arm32/include)
set(X86_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/x86/include)
set(AVR_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/avr/include)
set(MSP430_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/msp430/include)
set(MMIX_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/mmix/include)
set(LP64_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/lp64/include)
set(ILP32_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/ilp32/include)
set(IP16_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libc/ip16/include)
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
add_subdirectory(cpp)
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
include(scripts/CrossTools.cmake)
add_subdirectory(libc)
add_subdirectory(backend)
# The driver uses the cross tools each libc/<target> found.
add_subdirectory(cc)

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
# Installation: the compiler executables, renamed with a `v` prefix, and the runtime
# of each target under share/<target>.  The Makefile `install` target passes the
# prefix, ~/.local, via `cmake --install --prefix`.
#
install(PROGRAMS $<TARGET_FILE:parse> DESTINATION bin RENAME vparse)
install(PROGRAMS $<TARGET_FILE:lower> DESTINATION bin RENAME vlower)

#
# The test executables (every `*-tests` target) are left out of `all`, so a plain build
# and `make install` make only the compiler and its runtime; the `tests` target builds them.
#
add_custom_target(tests)
function(exclude_tests_from_all dir)
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        if(t MATCHES "-tests$")
            set_target_properties(${t} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            add_dependencies(tests ${t})
        endif()
    endforeach()
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        exclude_tests_from_all(${d})
    endforeach()
endfunction()
exclude_tests_from_all(${CMAKE_SOURCE_DIR})
