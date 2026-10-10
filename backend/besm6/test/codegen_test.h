#pragma once

#include <fstream>
#include <string>

#include "backend_test.h"
#include "besm.h"
#include "codegen.h"

extern "C" int xalloc_debug;

class CodegenTest : public BackendTest {
protected:
    CodegenTest() : BackendTest("besm6") {}

    // Capture Madlen output from a pre-built Besm_Module (used by Madlen-level tests).
    static std::string capture(const Besm_Module *module)
    {
        FILE *f = tmpfile();
        EXPECT_NE(nullptr, f);
        emit_madlen_module(f, module);
        long len = ftell(f);
        rewind(f);
        std::string result(static_cast<size_t>(len), '\0');
        EXPECT_TRUE(fread(&result[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return result;
    }

    // Capture emitted assembly from a pre-built TAC toplevel with full program context,
    // for the requested dialect.
    static std::string capture(const Tac_TopLevel *prog, const Tac_TopLevel *tl,
                               Besm_Dialect dialect = BESM_MADLEN)
    {
        FILE *f = tmpfile();
        EXPECT_NE(nullptr, f);
        codegen_program(prog, tl, f, dialect);
        long len = ftell(f);
        if (len == 0) {
            fclose(f);
            return {};
        }
        rewind(f);
        std::string result(static_cast<size_t>(len), '\0');
        EXPECT_TRUE(fread(&result[0], 1, static_cast<size_t>(len), f));
        fclose(f);
        return result;
    }

    // Backward-compatible overload: single pre-built toplevel acts as its own program.
    static std::string capture(const Tac_TopLevel *tl) { return capture(tl, tl); }

    // Parse C source, run full typecheck+translate+codegen pipeline, and return the
    // concatenated assembly (for the requested dialect) of every translated toplevel.
    std::string CompileTo(const char *src, Besm_Dialect dialect)
    {
        // The full chain is needed so frame_build can identify module-level names.
        Tac_TopLevel *all_tac = CompileToTac(src);

        // Phase 2: codegen each toplevel with the full program chain as context.
        std::string result;
        for (const Tac_TopLevel *t = all_tac; t; t = t->next)
            result += capture(all_tac, t, dialect);
        tac_free_toplevel(all_tac);
        return result;
    }

    std::string CompileToMadlen(const char *src) { return CompileTo(src, BESM_MADLEN); }
    std::string CompileToUnix(const char *src) { return CompileTo(src, BESM_UNIX); }
    std::string CompileToBemsh(const char *src) { return CompileTo(src, BESM_BEMSH); }

    // Compile C source, run it under the Dubna simulator, and return the program output.
    // Returns "ERROR" on compile failure, simulator failure, or malformed listing.
    std::string CompileAndRun(const std::string &src)
    {
        std::string madlen = CompileToMadlen(src.c_str());

        std::string job =
            "*name .\n"
            "*disc:1/local\n"
            "*file:libc,40\n"
            "*call setftn:one,long\n"
            "*assem\n";
        job += madlen;
        job +=
            "*library:40\n"
            "*execute\n"
            "*end file\n";

        std::string dub_path = ScratchPath(".dub");
        std::string lst_path = ScratchPath(".lst");

        // Held across the .dub write, the dubna run, and the .lst read; released by RAII
        // on every return below.  A failure to acquire means another besm-tests process
        // is running this same test concurrently and would clobber these files.
        FlockGuard dub_lock(dub_path);
        if (!dub_lock.locked()) {
            ADD_FAILURE() << "Concurrent besm-tests run detected for this test; do not "
                             "launch two besm-tests processes at once ("
                          << dub_path << ")";
            return "ERROR";
        }

        {
            std::ofstream dub(dub_path);
            if (!dub)
                return "ERROR";
            dub << job;
        }

        try {
            RunExternalProgram("dubna", { dub_path }, lst_path);
        } catch (...) {
            return "ERROR";
        }

        std::ifstream lst(lst_path);
        if (!lst)
            return "ERROR";
        std::string listing((std::istreambuf_iterator<char>(lst)), {});
        return ExtractDubnaOutput(listing);
    }

    // Extract a program's captured stdout from a Dubna `.lst`.  The monitor brackets the
    // program's own output between the second `≠` line (U+2260) and the trailing `----`
    // separator.  Shared by the Madlen (`*assem`) and Bemsh (`*bemsh`) run paths, which
    // produce the same monitor listing framing.  Returns "ERROR" if the markers are absent.
    static std::string ExtractDubnaOutput(const std::string &listing)
    {
        // Find second "≠" line (U+2260, UTF-8: 3 bytes).
        const std::string NE = "\xe2\x89\xa0";
        int ne_count         = 0;
        size_t pos           = 0;
        size_t content_start = std::string::npos;
        while (pos <= listing.size()) {
            size_t nl       = listing.find('\n', pos);
            size_t line_end = (nl == std::string::npos) ? listing.size() : nl;
            if (listing.substr(pos, line_end - pos) == NE) {
                if (++ne_count == 2) {
                    content_start = (nl == std::string::npos) ? listing.size() : nl + 1;
                    break;
                }
            }
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
        if (content_start == std::string::npos)
            return "ERROR";

        std::string content = listing.substr(content_start);

        // Truncate at the "----" separator line.
        pos = 0;
        while (pos < content.size()) {
            size_t nl       = content.find('\n', pos);
            size_t line_end = (nl == std::string::npos) ? content.size() : nl;
            if (content.substr(pos, line_end - pos).substr(0, 4) == "----") {
                content.resize(pos);
                break;
            }
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
        return content;
    }

    // Compile C source through the Bemsh dialect, run it under the Dubna simulator against
    // the Bemsh runtime library (libbem.bin), and return the program output.  The `*bemsh`
    // counterpart of CompileAndRun: genbesm --bemsh already wraps each module in its own
    // ввд$$$…кнц$$$ deck, so the job just adds the control cards around it (linking libbem on
    // library 40 and naming the `progra` entry — bemsh_mangle("program")).  Returns "ERROR"
    // on compile/simulator failure or a malformed listing.  Model: backend/besm6/tmp/bemsh.dub.
    std::string CompileAndRunBemsh(const std::string &src)
    {
        std::string bemsh = CompileToBemsh(src.c_str());

        std::string job =
            "*name .\n"
            "*disc:1/local\n"
            "*file:libbem,40\n"
            "*library:40\n"
            "*bemsh\n";
        job += bemsh;
        job +=
            "*main progra\n"
            "*execute\n"
            "*end file\n";

        std::string dub_path = ScratchPath(".dub");
        std::string lst_path = ScratchPath(".lst");

        // See CompileAndRun: guards against a concurrent besm-tests run clobbering the
        // shared per-test .dub/.lst files.
        FlockGuard dub_lock(dub_path);
        if (!dub_lock.locked()) {
            ADD_FAILURE() << "Concurrent besm-tests run detected for this test; do not "
                             "launch two besm-tests processes at once ("
                          << dub_path << ")";
            return "ERROR";
        }

        {
            std::ofstream dub(dub_path);
            if (!dub)
                return "ERROR";
            dub << job;
        }

        try {
            RunExternalProgram("dubna", { dub_path }, lst_path);
        } catch (...) {
            return "ERROR";
        }

        std::ifstream lst(lst_path);
        if (!lst)
            return "ERROR";
        std::string listing((std::istreambuf_iterator<char>(lst)), {});
        return ExtractDubnaOutput(listing);
    }

    // Compile C source through the Unix (b6as) path, assemble it with b6as, and link it
    // with b6ld against libc0.a + libruntime.a.  Asserts each external step exits 0 (non-fatal
    // EXPECT, with the tool's captured diagnostics on failure).  Returns the emitted .s
    // text so a caller may additionally golden-diff it.  Execution under b6sim is out of
    // scope (tasks U5/U6) — this only proves the assembly assembles and links cleanly.
    std::string CompileAndAssembleUnix(const std::string &src)
    {
        std::string asm_text = CompileToUnix(src.c_str());

        std::string base     = ScratchPath("");
        std::string s_path   = base + ".s";
        std::string o_path   = base + ".o";
        std::string exe_path = base + ".b6";
        std::string as_log   = base + ".aslog";
        std::string ld_log   = base + ".ldlog";

        // Held across the .s write, the assemble, and the link; released by RAII on every
        // return below.  A failure to acquire means another besm-tests process is running
        // this same test concurrently and would clobber these shared scratch files.
        FlockGuard lock(s_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent besm-tests run detected for this test; do not "
                             "launch two besm-tests processes at once ("
                          << s_path << ")";
            return asm_text;
        }

        {
            std::ofstream s(s_path);
            if (!s) {
                ADD_FAILURE() << "Cannot write " << s_path;
                return asm_text;
            }
            s << asm_text;
        }

        // genbesm --unix already ran in-process via CompileToUnix; now assemble, then link.
        int as_rc = RunTool({ "b6as", "-o", o_path, s_path }, as_log);
        EXPECT_EQ(0, as_rc) << "b6as failed on " << s_path << ":\n" << ReadFile(as_log);
        if (as_rc != 0)
            return asm_text;

        // Objects first, then the archives, so back-references resolve via the b6ranlib
        // index; libruntime.a (the b$* helpers) goes last because libc0.a calls into it
        // and nothing in it calls back.  Both are staged in the test's working directory
        // (build/backend/besm6), which besm-tests chdir()s into at startup, so plain
        // relative paths suffice.
        int ld_rc = RunTool({ "b6ld", "-o", exe_path, o_path, "libc0.a", "libruntime.a" }, ld_log);
        EXPECT_EQ(0, ld_rc) << "b6ld failed linking " << o_path << ":\n" << ReadFile(ld_log);

        return asm_text;
    }

    // Compile C source through the Unix (b6as) path, assemble with b6as, link with b6ld
    // against libc0.a + libruntime.a, then run the executable under the b6sim simulator and
    // return its captured stdout.  The Unix-path counterpart of CompileAndRun (which uses the
    // Madlen .mad → dubna .lst path): b6sim writes the program's write(1,…) output straight to
    // stdout, so there is no listing to scrape.  Returns "ERROR" on any tool failure.
    std::string CompileAndRunUnix(const std::string &src)
    {
        std::string asm_text = CompileToUnix(src.c_str());

        std::string base     = ScratchPath("");
        std::string s_path   = base + ".s";
        std::string o_path   = base + ".o";
        std::string exe_path = base + ".b6";
        std::string out_path = base + ".out";
        std::string as_log   = base + ".aslog";
        std::string ld_log   = base + ".ldlog";

        // Held across the .s write, assemble, link, and run; released by RAII on every
        // return below.  A failure to acquire means another besm-tests process is running
        // this same test concurrently and would clobber these shared scratch files.
        FlockGuard lock(s_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent besm-tests run detected for this test; do not "
                             "launch two besm-tests processes at once ("
                          << s_path << ")";
            return "ERROR";
        }

        {
            std::ofstream s(s_path);
            if (!s) {
                ADD_FAILURE() << "Cannot write " << s_path;
                return "ERROR";
            }
            s << asm_text;
        }

        int as_rc = RunTool({ "b6as", "-o", o_path, s_path }, as_log);
        EXPECT_EQ(0, as_rc) << "b6as failed on " << s_path << ":\n" << ReadFile(as_log);
        if (as_rc != 0)
            return "ERROR";

        // crt0.o first: b6ld takes the entry point from the first object's first text word,
        // so the C startup object must lead, ahead of the program object and the archives
        // (libruntime.a last — libc0.a calls the b$* helpers, not the other way round).
        // All are staged in the working directory (build/backend/besm6), which besm-tests
        // chdir()s into at startup, so plain relative names suffice.
        int ld_rc = RunTool({ "b6ld", "-o", exe_path, "crt0.o", o_path, "libc0.a", "libruntime.a" },
                            ld_log);
        EXPECT_EQ(0, ld_rc) << "b6ld failed linking " << o_path << ":\n" << ReadFile(ld_log);
        if (ld_rc != 0)
            return "ERROR";

        // Run under the simulator, capturing the program's stdout to out_path.
        try {
            RunExternalProgram("b6sim", { exe_path }, out_path);
        } catch (...) {
            return "ERROR";
        }
        return ReadFile(out_path);
    }

    // Run a "Writing a C Compiler" book program through the Unix path.  Like
    // CompileAndRunUnix, but runs b6sim with --status so the simulator prints
    // main()'s return value (a 41-bit signed integer, "%d\n") after the program's
    // own stdout — reproducing what the book's return-value tests expect without a
    // source wrapper.  The program's entry stays named `main`, so the compiler's
    // C11 §5.1.2.2.3 implicit `return 0` for a fall-through main still applies.
    // Returns "ERROR" on any tool failure.
    std::string CompileAndRunBook(const std::string &src)
    {
        std::string asm_text = CompileToUnix(src.c_str());

        std::string base     = ScratchPath("");
        std::string s_path   = base + ".s";
        std::string o_path   = base + ".o";
        std::string exe_path = base + ".b6";
        std::string out_path = base + ".out";
        std::string as_log   = base + ".aslog";
        std::string ld_log   = base + ".ldlog";

        // Held across the .s write, assemble, link, and run; released by RAII on every
        // return below.  A failure to acquire means another besm-tests process is running
        // this same test concurrently and would clobber these shared scratch files.
        FlockGuard lock(s_path);
        if (!lock.locked()) {
            ADD_FAILURE() << "Concurrent besm-tests run detected for this test; do not "
                             "launch two besm-tests processes at once ("
                          << s_path << ")";
            return "ERROR";
        }

        {
            std::ofstream s(s_path);
            if (!s) {
                ADD_FAILURE() << "Cannot write " << s_path;
                return "ERROR";
            }
            s << asm_text;
        }

        int as_rc = RunTool({ "b6as", "-o", o_path, s_path }, as_log);
        EXPECT_EQ(0, as_rc) << "b6as failed on " << s_path << ":\n" << ReadFile(as_log);
        if (as_rc != 0)
            return "ERROR";

        // crt0.o first: b6ld takes the entry point from the first object's first text
        // word, so the C startup object must lead, ahead of the program object and the
        // archives (libruntime.a last — libc0.a calls the b$* helpers, not the other way
        // round).  All are staged in the working directory (build/backend/besm6), which
        // besm-tests chdir()s into at startup, so plain relative names suffice.
        int ld_rc = RunTool({ "b6ld", "-o", exe_path, "crt0.o", o_path, "libc0.a", "libruntime.a" },
                            ld_log);
        EXPECT_EQ(0, ld_rc) << "b6ld failed linking " << o_path << ":\n" << ReadFile(ld_log);
        if (ld_rc != 0)
            return "ERROR";

        // Run under the simulator with --status so it appends main()'s return value
        // ("%d\n") to the program's own stdout, captured to out_path.  The flag must come
        // BEFORE the executable: b6sim's optstring starts with '+', so getopt stops at the
        // first non-option argument and everything after the program file is the *guest's*
        // argv.  b6sim exits with the guest's return value, so ignore_exit_status must be
        // set.
        try {
            RunExternalProgram("b6sim", { "--status", exe_path }, out_path,
                               /*ignore_exit_status=*/true);
        } catch (...) {
            return "ERROR";
        }
        return ReadFile(out_path);
    }
};

// Skip a Unix assemble+link test when the sibling v7besm b6as/b6ld tools are not installed
// on PATH, so `make run` stays green on machines without that toolchain.  Must be used at
// test-body scope: GTEST_SKIP()'s early return exits the whole test, not just a helper.
#define SKIP_IF_NO_UNIX_TOOLS()                                                        \
    do {                                                                               \
        if (!tool_available("b6as") || !tool_available("b6ld"))                        \
            GTEST_SKIP() << "b6as/b6ld not on PATH; skipping Unix assemble+link test"; \
    } while (0)

// Like SKIP_IF_NO_UNIX_TOOLS() but also requires the b6sim simulator, for the Unix run
// harness (CompileAndRunUnix) which additionally executes the linked b.out.
#define SKIP_IF_NO_UNIX_RUN_TOOLS()                                                         \
    do {                                                                                    \
        if (!tool_available("b6as") || !tool_available("b6ld") || !tool_available("b6sim")) \
            GTEST_SKIP() << "b6as/b6ld/b6sim not on PATH; skipping Unix run test";          \
    } while (0)

//
// Shared helper for the imported "Writing a C Compiler" run tests
// (chapterNN_tests.cpp).
//
// The book's positive programs define `int main(void)` and are validated by
// their return value (an exit code, or 0 on success for self-checking tests).
// The BESM-6 libc entry point calls `void program()`, so we wrap each program
// with a program() that prints main()'s return value; the expected value is the
// stdout of the same wrapped source compiled and run with the host compiler.
//
// Wrap a book program so program() prints `main()`'s return value as "%d\n".
inline std::string WrapMain(const std::string &program)
{
    return "int printf(const char *format, ...);\n" + program +
           "\nvoid program(void) { printf(\"%d\\n\", main()); }\n";
}
