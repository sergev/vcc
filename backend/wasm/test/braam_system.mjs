// wasm32-braam on Braam itself (backend/wasm/Plan.md §7.5): programs built by vcc,
// planted in /bin of a session of braam-core's system harness and run from its shell.
//
//     node braam_system.mjs <braam-core> <work-dir> <vcc> <docs/examples>
//
// <braam-core> is a built checkout (build/kernel.wasm, build/web/rootfs.zip); <vcc> is
// the driver, run with -t wasm32-braam, and the environment it is run in (VCC_* for an
// in-tree build).  It runs test/system/abi.mjs over the binaries, then each program:
// hello's arguments and status, cat and wc on a file and through a redirection, the
// worked example, notes, with a session typed at the terminal, and ^C: caught by a read
// of the terminal with a second task sleeping, not caught, and caught by a loop that
// parks with braam_yield.  Prints a line for
// each case and exits 1 at the first failure.
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [core, work, vcc, examples] = process.argv.slice(2).map((p) => p && resolve(p));
if (!examples) {
    console.error("usage: node braam_system.mjs <braam-core> <work-dir> <vcc> <examples>");
    process.exit(2);
}

const sources = {
    hello: `#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    printf("hello from %s:", argv[0]);
    for (int i = 1; i < argc; i++)
        printf(" [%s]", argv[i]);
    printf("\\n");
    return argc;
}
`,
    cat: `#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    int status = 0;
    for (int i = 1; i < argc || i == 1; i++) {
        FILE *f = argc == 1 ? stdin : await fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        char buf[512];
        size_t n;
        while ((n = await fread(buf, 1, sizeof buf, f)) > 0)
            fwrite(buf, 1, n, stdout);
        if (f != stdin)
            await fclose(f);
    }
    return status;
}
`,
    wc: `#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? await fopen(argv[1], "r") : stdin;
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    int c, lines = 0, words = 0, chars = 0, in = 0;
    while ((c = await getc(f)) != EOF) {
        chars++;
        lines += c == '\\n';
        if (c == ' ' || c == '\\n' || c == '\\t')
            in = 0;
        else if (!in) {
            in = 1;
            words++;
        }
    }
    printf("%d %d %d\\n", lines, words, chars);
    return 0;
}
`,
    intr: `#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static coro(braam_call *) int ticker(void)
{
    while (await sleep_ms(5) == 0)
        ;
    printf("ticker: %s\\n", strerror(errno));
    return 0;
}
coro(braam_call *) int main(int argc, char **argv)
{
    static char storage[4096];
    braam_spawn(co_init(storage, sizeof storage, ticker));
    if (await sig_catch(SIGINT, 1) < 0)
        return 1;
    char buf[64];
    ssize_t n = await read(0, buf, sizeof buf);
    printf("read %d, %s, took %d\\n", (int)n, strerror(errno), sig_take(SIGINT));
    n = await read(0, buf, sizeof buf);
    printf("then read %d\\n", (int)n);
    return 0;
}
`,
    nocatch: `#include <stdio.h>
#include <unistd.h>
coro(braam_call *) int main(int argc, char **argv)
{
    char buf[8];
    await read(0, buf, sizeof buf);
    printf("not reached\\n");
    return 0;
}
`,
    spin: `#include <signal.h>
#include <stdio.h>
coro(braam_call *) int main(int argc, char **argv)
{
    if (await sig_catch(SIGINT, 1) < 0)
        return 1;
    unsigned n = 0;
    while (!sig_take(SIGINT)) {
        for (int i = 0; i < 100000; i++)
            n++;
        await braam_yield();
    }
    printf("spin stopped %d\\n", n > 0);
    return 0;
}
`,
};

mkdirSync(work, { recursive: true });
const bins = {};
for (const [name, src] of Object.entries(sources)) {
    writeFileSync(join(work, name + ".c"), src);
    bins[name] = join(work, name);
    execFileSync(vcc, ["-t", "wasm32-braam", join(work, name + ".c"), "-o", bins[name]],
                 { stdio: "inherit" });
}
bins.notes = join(work, "notes");
execFileSync(vcc, ["-t", "wasm32-braam", join(examples, "notes.c"), "-o", bins.notes],
             { stdio: "inherit" });

// The harness prints the kernel's log lines; only ours matter here.
const print = console.log.bind(console);
console.log = () => {};
const H = await import(pathToFileURL(join(core, "test/system/harness.mjs")));
const abi = await import(pathToFileURL(join(core, "test/system/abi.mjs")));
await H.init(join(core, "build/kernel.wasm"), join(core, "build/web/rootfs.zip"));
H.kernel().init(0);
if (H.run(0) !== -1)
    H.fail("the shell did not park on the keyboard");
H.regrid(80, 24, "resize returned no screen descriptor");

function die(msg) {
    console.error("braam_system: " + msg);
    process.exit(1);
}

abi.check(Object.values(bins));
print("ok abi: " + Object.keys(bins).join(", "));
for (const [name, path] of Object.entries(bins))
    H.store.files.set("/bin/" + name, new Uint8Array(readFileSync(path)));

let now = 1;
const text = (path) => new TextDecoder().decode(H.store.files.get(path) ?? new Uint8Array());
const plant = (path, s) => H.store.files.set(path, new TextEncoder().encode(s));

// A command line, its stdout to /tmp/o and its status to /tmp/s; what it printed.
function run(cmd) {
    const line = `${cmd} >/tmp/o; echo $? >/tmp/s`;
    if (line.length > 64)
        die(`the command line is ${line.length} keys, and the ring holds 64`);
    H.submit(line, now++);
    if (H.run(now++) !== -1)
        die("the kernel did not settle after: " + cmd);
    return text("/tmp/o");
}

function expect(what, got, want) {
    if (got !== want)
        die(`${what}: got ${JSON.stringify(got)}, expected ${JSON.stringify(want)}`);
}

const status = () => Number(text("/tmp/s").trim());

expect("hello", run("hello one two"), "hello from hello: [one] [two]\n");
expect("hello's status", status(), 3);
print("ok hello");

let input = "";
for (let i = 1; i <= 2000; i++)
    input += i + (i % 9 ? " " : "\n");
plant("/tmp/in", input);
expect("cat of a file", run("cat /tmp/in"), input);
expect("cat of stdin", run("cat </tmp/in"), input);
run("cat /tmp/none");
expect("cat of nothing", status(), 1);
print("ok cat");

const words = input.split(/\s+/).filter(Boolean).length;
const want = `${input.split("\n").length - 1} ${words} ${input.length}\n`;
expect("wc of a file", run("wc /tmp/in"), want);
expect("wc of stdin", run("wc </tmp/in"), want);
print("ok wc");

// The worked example, as docs/Braam_Example.md runs it.
H.submit("cd /tmp", now++);
expect("notes, none", run("notes"), "no notes\n");
run("notes add buy milk");
run("notes add call the plumber");
H.submit("notes ask", now++);
H.submit("water plants", now++);
H.submit("write letter", now++);
H.press("d".codePointAt(0), H.CTRL);
if (H.run(now++) !== -1)
    die("notes ask did not end at ^D");
const screen = H.rows(H.screen()).join("\n");
if (!screen.includes("note? water plants\nnote? write letter\nnote?\n2 added"))
    die("notes ask's session:\n" + screen);
run("notes del 2");
expect("notes del 9", run("notes del 9"), "there is no note 9\n");
expect("its status", status(), 1);
expect("notes", run("notes"),
       "  1  buy milk\n  2  water plants\n  3  write letter\n(3 notes, 35 bytes)\n");
expect("notes.txt", text("/tmp/notes.txt"), "buy milk\nwater plants\nwrite letter\n");
print("ok notes");

// ^C at the terminal.  Caught: the read and the second task's sleep give up with EINTR,
// and the program reads on.  Not caught: the process ends with 130.
const ctrlC = () => H.press("c".codePointAt(0), H.CTRL);
const screenText = () => H.rows(H.screen()).join("\n");
H.submit("intr", now++);
ctrlC();
H.run(now++);
H.submit("more", now++);
if (H.run(now++) !== -1)
    die("intr did not settle");
if (!/ticker: Interrupted\nread -1, Interrupted, took 1\nmore\nthen read 5\n/.test(screenText()))
    die("intr's session:\n" + screenText());
H.submit("nocatch; echo $? >/tmp/s", now++);
ctrlC();
if (H.run(now++) !== -1)
    die("nocatch did not settle");
expect("nocatch's status", screenText().includes("[130]"), true);

// A loop that never waits on anything still parks once a burst, in braam_yield, which
// is where the ^C reaches it.  The kernel always has work then, so it is pumped a
// fixed number of ticks rather than until it settles.
H.type("spin");
H.press(H.KEY.ENTER);
for (let i = 0; i < 30; i++, now++) {
    H.kernel().tick(now);
    while (H.net.drain())
        H.kernel().tick(now);
}
ctrlC();
if (H.run(now++) !== -1)
    die("spin did not stop at ^C");
if (!screenText().includes("spin stopped 1"))
    die("spin's session:\n" + screenText());
print("ok signals");
