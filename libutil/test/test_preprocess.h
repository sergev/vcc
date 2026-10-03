// Shared test helper: run a C source snippet through the system C preprocessor.
//
// The compiler has no built-in preprocessor, so test fixtures that want to
// #include the target's standard headers (e.g. <stdio.h>) must expand the
// directives first.  This header generalises the original per-test Preprocess()
// (see backend/besm6/stdarg_tests.cpp) into one inline helper shared by every
// source-compiling fixture.
//
// The preprocessor (the C compiler, invoked with -E) and the include directory
// come from CMake via the TEST_CPP, TEST_INCLUDE_DIR (the target's headers) and
// TEST_COMMON_INCLUDE_DIR (the shared ones, searched last) compile definitions, and
// the optional TEST_TARGET_INCLUDE_DIR, searched first (a target's own headers while
// it borrows the rest from another's), and the optional TEST_MODEL_INCLUDE_DIR, searched between them (the data-model headers:
// libc/lp64 for riscv64 and aarch64, libc/ilp32 for riscv32); the macros are expanded in the including test translation unit, so only test
// targets need to define them.
//
// Preprocessing is conditional: a snippet with no preprocessing directive is
// returned verbatim, so directive-free tests never fork the preprocessor and see
// no comment stripping or line-number shifts.
#pragma once

#include <unistd.h>

#include <cstdio>
#include <string>

#ifndef TEST_CPP
#error "TEST_CPP must be defined (the C compiler, invoked with -E) to use test_preprocess.h"
#endif
#ifndef TEST_INCLUDE_DIR
#error "TEST_INCLUDE_DIR must be defined (the target's standard headers) to use test_preprocess.h"
#endif
#ifndef TEST_COMMON_INCLUDE_DIR
#error "TEST_COMMON_INCLUDE_DIR must be defined (the shared standard headers) to use test_preprocess.h"
#endif

// True if some line of SRC has '#' as its first non-whitespace character, i.e.
// SRC contains a preprocessing directive.  In-literal hashes like "%#x" sit
// mid-line and are correctly ignored.
inline bool source_has_directive(const std::string &src)
{
    bool at_line_start = true;
    for (char c : src) {
        if (c == '\n') {
            at_line_start = true;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            // stay at line start through leading whitespace
        } else {
            if (at_line_start && c == '#')
                return true;
            at_line_start = false;
        }
    }
    return false;
}

// Expand SRC with the system C preprocessor against the BESM-6 include dir.
// Directive-free input is returned unchanged.  On cpp/popen failure an empty
// string is returned (callers GTEST_SKIP() in that case).
inline std::string preprocess_source(const std::string &src)
{
    if (!source_has_directive(src))
        return src;

    char path[] = "/tmp/besm6_src_XXXXXX";
    int fd      = mkstemp(path);
    if (fd < 0)
        return {};
    bool ok = write(fd, src.data(), src.size()) == static_cast<ssize_t>(src.size());
    close(fd);

    std::string out;
    if (ok) {
        // -E: preprocess only; -x c: the mkstemp file has no .c suffix.  Line
        // markers are kept (no -P): our scanner consumes them and they preserve
        // original line numbers for diagnostics.
        std::string cmd = TEST_CPP " -E -x c -nostdinc";
#ifdef TEST_TARGET_INCLUDE_DIR
        cmd += " -I" TEST_TARGET_INCLUDE_DIR;
#endif
        cmd += " -I" TEST_INCLUDE_DIR;
#ifdef TEST_MODEL_INCLUDE_DIR
        cmd += " -I" TEST_MODEL_INCLUDE_DIR;
#endif
        cmd += " -I" TEST_COMMON_INCLUDE_DIR " ";
        cmd += path;
        cmd += " 2>/dev/null";

        FILE *p = popen(cmd.c_str(), "r");
        if (p) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
                out.append(buf, n);
            if (pclose(p) != 0)
                out.clear();
        }
    }
    unlink(path);
    return out;
}
