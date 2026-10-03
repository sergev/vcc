// Fixture for a backend whose programs run on bare-metal qemu: assemble our output and
// compile any clang part, link with ld.lld against the target's crt0 and libc.a, run
// qemu under a timeout, and return the UART output with main's result (the qemu exit
// status) in exit_status.  A backend fixture derives from it with a QemuConfig.
#pragma once

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "backend_test.h"

struct QemuConfig {
    const char *suite;                     // for diagnostics, e.g. "riscv-tests"
    const char *clang;                     // assembles, and compiles the clang parts
    std::vector<std::string> target_flags; // --target=… and the ABI, for both
    std::vector<std::string> c_flags;      // more for C, e.g. -ffreestanding
    const char *ld;                        // ld.lld
    const char *link_script;
    const char *lib_dir;           // crt0 objects and libc.a
    std::vector<std::string> qemu; // the command up to -kernel <exe>
    const char *scratch_suffix;    // keeps the scratch files of two widths apart
    // Where main's result comes from: false, qemu's exit status (semihosting); true,
    // the first byte written to the debug console (port 0xe9 on x86), which the run
    // sends to a file of its own.  qemu's x86 exit device cannot carry a whole byte.
    bool status_from_debugcon = false;
};

class QemuTest : public BackendTest {
    QemuConfig config;

protected:
    int exit_status = -1; // of the last run: main's result, modulo 256

    QemuTest(const char *target, QemuConfig cfg) : BackendTest(target), config(std::move(cfg)) {}

    // Scratch file of the current test: TEST_DIR/<Suite>.<Test><suffix><tag>.  The suite
    // keeps apart tests of one name in two suites.
    std::string QemuScratchPath(const std::string &tag) const
    {
        const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        return std::string(TEST_DIR "/") + info->test_suite_name() + "." + info->name() +
               config.scratch_suffix + tag;
    }

    // Assemble `asm_text` (unless empty) and compile `clang_src` (if any) with
    // `clang_flags`, link with `crt0` and run under qemu.  Scratch files are named by
    // the test and `tag`.  Returns the UART output, or "ERROR".
    std::string Run(const std::string &asm_text, const char *crt0,
                    const std::string *clang_src                = nullptr,
                    const std::vector<std::string> &clang_flags = {}, const char *tag = "")
    {
        exit_status          = -1;
        std::string base     = QemuScratchPath(tag);
        std::string s_path   = base + ".s";
        std::string o_path   = base + ".o";
        std::string exe_path = base + ".elf";
        std::string out_path = base + ".out";
        std::string log_path = base + ".log";

        // Guards the scratch files against a concurrent run of the same test.
        FlockGuard lock(s_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent " << config.suite << " run detected (" << s_path << ")";
            return "ERROR";
        }
        std::vector<std::string> objs;
        int rc;
        if (!asm_text.empty()) {
            {
                std::ofstream s(s_path);
                s << asm_text;
            }
            std::vector<std::string> as = { config.clang };
            as.insert(as.end(), config.target_flags.begin(), config.target_flags.end());
            as.insert(as.end(), { "-c", "-o", o_path, s_path });
            rc = RunTool(as, log_path);
            EXPECT_EQ(0, rc) << "assembler failed on " << s_path << ":\n" << ReadFile(log_path);
            if (rc != 0)
                return "ERROR";
            objs.push_back(o_path);
        }
        if (clang_src) {
            std::string c_path  = base + "-clang.c";
            std::string co_path = base + "-clang.o";
            {
                std::ofstream c(c_path);
                c << *clang_src;
            }
            std::vector<std::string> cc = { config.clang };
            cc.insert(cc.end(), config.target_flags.begin(), config.target_flags.end());
            cc.insert(cc.end(), config.c_flags.begin(), config.c_flags.end());
            cc.insert(cc.end(), { "-c", "-o", co_path });
            cc.insert(cc.end(), clang_flags.begin(), clang_flags.end());
            cc.push_back(c_path);
            rc = RunTool(cc, log_path);
            EXPECT_EQ(0, rc) << "clang failed on " << c_path << ":\n" << ReadFile(log_path);
            if (rc != 0)
                return "ERROR";
            objs.push_back(co_path);
        }
        std::string lib               = config.lib_dir;
        std::vector<std::string> link = { config.ld, "-T",     config.link_script,
                                          "-o",      exe_path, lib + "/" + crt0 };
        link.insert(link.end(), objs.begin(), objs.end());
        link.push_back(lib + "/libc.a");
        rc = RunTool(link, log_path);
        EXPECT_EQ(0, rc) << "ld.lld failed on " << exe_path << ":\n" << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        std::string status_path       = base + ".status";
        std::vector<std::string> qemu = config.qemu;
        if (config.status_from_debugcon) {
            std::remove(status_path.c_str());
            qemu.insert(qemu.end(), { "-debugcon", "file:" + status_path });
        }
        qemu.insert(qemu.end(), { "-kernel", exe_path });
        rc = RunWithTimeout(qemu, out_path, log_path, 5);
        if (rc < 0) {
            ADD_FAILURE() << (rc == -2 ? "qemu timed out" : "qemu failed") << " on " << exe_path
                          << ":\n"
                          << ReadFile(log_path);
            return "ERROR";
        }
        exit_status = rc;
        if (config.status_from_debugcon) {
            std::string status = ReadFile(status_path);
            if (status.empty()) {
                ADD_FAILURE() << "no exit status on the debug console of " << exe_path << ":\n"
                              << ReadFile(log_path);
                return "ERROR";
            }
            exit_status = (unsigned char)status[0];
        }
        return ReadFile(out_path);
    }
};
