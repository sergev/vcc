// Fixture for a backend whose programs run on bare-metal qemu: assemble our output and
// compile any clang part, link with ld.lld against the target's crt0 and libc.a, run
// qemu under a timeout, and return the UART output with main's result (the qemu exit
// status) in exit_status.  A backend fixture derives from it with a QemuConfig.  The
// runner need not be qemu: MSP430 runs on the mspsim simulator, through the same steps.
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
    std::vector<std::string> qemu; // the command up to -kernel <exe> (or image_option)
    const char *scratch_suffix;    // keeps the scratch files of two widths apart
    // Where main's result comes from: false, qemu's exit status (semihosting); true,
    // the first byte written to the debug console (port 0xe9 on x86), which the run
    // sends to a file of its own.  qemu's x86 exit device cannot carry a whole byte.
    bool status_from_debugcon = false;
    // The option that loads the image: -kernel, or -bios on AVR, where -kernel loads
    // nothing; empty when the image is a plain argument (mspsim).
    const char *image_option = "-kernel";
    // Main's result is the first byte on a second serial port (USART1 on AVR), which the
    // run sends to a file of its own.  Nothing on qemu's AVR machine makes it exit, so
    // the run ends when that byte arrives.
    bool status_from_serial = false;
    // More linker options, before the objects: -n on MSP430, so that ld.lld loads no
    // ELF header into the peripheral area at address 0.
    std::vector<std::string> link_flags = {};
    // The runner's exit status is main's result only when its log has the
    // "[Exit code N after M cycles]" line, mspsim's report of a program that stopped
    // itself; any other stop (the cycle limit, an illegal instruction, a CPU asleep for
    // good) has a status of its own, which a result could collide with, and fails the run.
    bool exit_report = false;
    // Libraries linked after libc.a: libgcc.a on MSP430, for the helpers GCC's code
    // calls beyond ours.
    std::vector<std::string> extra_libs = {};
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
        return Run(config, asm_text, crt0, clang_src, clang_flags, tag);
    }

    // The same with another toolchain: MSP430 also links with clang and ld.lld.
    std::string Run(const QemuConfig &cfg, const std::string &asm_text, const char *crt0,
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
            ADD_FAILURE() << "Concurrent " << cfg.suite << " run detected (" << s_path << ")";
            return "ERROR";
        }
        std::vector<std::string> objs;
        int rc;
        if (!asm_text.empty()) {
            {
                std::ofstream s(s_path);
                s << asm_text;
            }
            std::vector<std::string> as = { cfg.clang };
            as.insert(as.end(), cfg.target_flags.begin(), cfg.target_flags.end());
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
            std::vector<std::string> cc = { cfg.clang };
            cc.insert(cc.end(), cfg.target_flags.begin(), cfg.target_flags.end());
            cc.insert(cc.end(), cfg.c_flags.begin(), cfg.c_flags.end());
            cc.insert(cc.end(), { "-c", "-o", co_path });
            cc.insert(cc.end(), clang_flags.begin(), clang_flags.end());
            cc.push_back(c_path);
            rc = RunTool(cc, log_path);
            EXPECT_EQ(0, rc) << "clang failed on " << c_path << ":\n" << ReadFile(log_path);
            if (rc != 0)
                return "ERROR";
            objs.push_back(co_path);
        }
        std::string lib               = cfg.lib_dir;
        std::vector<std::string> link = { cfg.ld };
        link.insert(link.end(), cfg.link_flags.begin(), cfg.link_flags.end());
        link.insert(link.end(), { "-T", cfg.link_script, "-o", exe_path, lib + "/" + crt0 });
        link.insert(link.end(), objs.begin(), objs.end());
        link.push_back(lib + "/libc.a");
        link.insert(link.end(), cfg.extra_libs.begin(), cfg.extra_libs.end());
        rc = RunTool(link, log_path);
        EXPECT_EQ(0, rc) << "ld.lld failed on " << exe_path << ":\n" << ReadFile(log_path);
        if (rc != 0)
            return "ERROR";
        std::string status_path       = base + ".status";
        std::vector<std::string> qemu = cfg.qemu;
        std::remove(status_path.c_str());
        if (cfg.status_from_debugcon)
            qemu.insert(qemu.end(), { "-debugcon", "file:" + status_path });
        if (cfg.status_from_serial)
            qemu.insert(qemu.end(), { "-serial", "file:" + status_path });
        if (*cfg.image_option)
            qemu.push_back(cfg.image_option);
        qemu.push_back(exe_path);
        rc = RunWithTimeout(qemu, out_path, log_path, 5,
                            cfg.status_from_serial ? status_path : std::string());
        if (rc < 0) {
            ADD_FAILURE() << (rc == -2 ? "qemu timed out" : "qemu failed") << " on " << exe_path
                          << ":\n"
                          << ReadFile(log_path);
            return "ERROR";
        }
        exit_status = rc;
        if (cfg.exit_report && ReadFile(log_path).find("[Exit code ") == std::string::npos) {
            ADD_FAILURE() << "the program did not stop itself (status " << rc << ") on "
                          << exe_path << ":\n"
                          << ReadFile(log_path);
            return "ERROR";
        }
        if (cfg.status_from_debugcon || cfg.status_from_serial) {
            std::string status = ReadFile(status_path);
            if (status.empty()) {
                ADD_FAILURE() << "no exit status on the "
                              << (cfg.status_from_serial ? "status serial port"
                                                            : "debug console")
                              << " of " << exe_path << ":\n"
                              << ReadFile(log_path);
                return "ERROR";
            }
            exit_status = (unsigned char)status[0];
        }
        return ReadFile(out_path);
    }
};
