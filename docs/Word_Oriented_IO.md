# Word-Oriented I/O (`libutil/wio`)

## The problem: programs in a pipeline must share structured data

The compiler is split into separate executables, each responsible for one stage:

```text
source.c  →  [parse]  →  binary AST  →  [lower]  →  binary TAC  →  [codegen]
```

`parse` reads a C source file and builds an Abstract Syntax Tree (AST) — a web of interconnected C structs in memory. It must pass that tree to `lower`, which type-checks it and translates it to Three-Address Code (TAC). `lower` in turn writes TAC so a target code generator (`genriscv`, `genbesm`) can turn it into machine instructions.

The same `wio` layer handles both hand-offs: AST from `parse` to `lower`, and TAC from `lower` to the code generator.

The catch at every stage: **two processes cannot share memory**. When `parse` finishes, every pointer it holds is gone. The only way to pass the tree to the next stage is to *serialize* it — flatten the tree into a stream of bytes, write those bytes to a file or pipe, and let the next program reconstruct the tree by reading them back.

What format should those bytes take?

- **Text (YAML, JSON)** is human-readable but slow to write, slow to parse, and lossy for binary values like floating-point constants.
- **Raw `fwrite` of structs** is fast but breaks on pointers: a pointer is just a memory address, and it means nothing in a different process.
- **A custom binary protocol** is the right answer — compact, exact, and tailored to the data.

`wio` is that protocol's I/O layer.

## Why work in words, not bytes?

Every value that needs to cross the pipe — a node-type tag, an enum value, an array length, a boolean flag, an integer constant — fits comfortably in one machine word. On a 64-bit system that is 8 bytes; on a 32-bit system, 4 bytes. In C this type is called `size_t`.

Committing to `size_t`-sized units has four concrete benefits:

1. **No partial reads.** A read that yields a non-multiple of `sizeof(size_t)` is definitely corrupt data, so `wio` can catch it immediately.
2. **No struct-padding ambiguity.** Compiler-inserted padding between struct fields varies by platform. Writing word by word sidesteps the issue entirely.
3. **Simple position arithmetic.** Seek positions are in word units, so `wtell()` returns the number of words read or written — easy to reason about.
4. **Easy hex-dump debugging.** Each line of `xxd` output shows one value cleanly aligned.

## The WFILE structure

`wio` defines `WFILE`, which mirrors the standard library's `FILE *` — but for words:

```c
struct _wfile {
    int     fd;             // underlying file descriptor
    size_t *buffer;         // in-memory word buffer
    size_t  buffer_pos;     // next slot to read from or write into
    size_t  buffer_count;   // words currently in the buffer (read mode)
    bool    is_eof;         // hit end of stream cleanly
    bool    is_error;       // something went wrong
    bool    must_close_fd;  // true if wio opened the fd itself
    char    mode;           // 'r', 'w', or 'a'
};
```

The buffer holds `4096 / sizeof(size_t)` words — exactly one OS page worth of data. This means a single `read()` or `write()` system call fills or drains the buffer, keeping the number of kernel transitions low.

## Opening a stream

There are two ways to open a `WFILE`:

- **`wopen(stream, path, mode)`** — opens a named file, just like `fopen`. Mode `"r"` reads, `"w"` writes (truncating), `"a"` appends.
- **`wdopen(stream, fd, mode)`** — wraps an *existing* file descriptor. This is what the pipeline uses: `parse` writes to file descriptor 1 (stdout) and `lower` reads from file descriptor 0 (stdin). No temporary files, no named pipes — the OS connects them directly.

`wclose()` flushes any buffered writes and frees the buffer. If `wio` opened the file itself (`must_close_fd == true`), it also closes the file descriptor; if the caller supplied an existing fd, it leaves it open.

## Reading and writing words: `wgetw` / `wputw`

Writing is straightforward:

```c
wputw(TAG_PROGRAM, &fd);   // buffer one word
wputw(42, &fd);            // buffer another
wclose(&fd);               // flush to disk
```

`wputw` stores the word in the buffer. When the buffer fills up (`buffer_pos >= BUFFER_SIZE`), it calls `write()` to drain it to the OS, then continues. `wclose` performs a final flush for anything left over.

Reading is the mirror image. `wgetw` checks whether the buffer has any words left. If not, it calls `read()` to fill it — up to one page at a time. It validates that the number of bytes returned is an exact multiple of `sizeof(size_t)`. If not, something is corrupt, and `is_error` is set. On clean end-of-file, `is_eof` is set instead. Both conditions cause `wgetw` to return `(size_t)-1`, a value that cannot be a valid data word (it means "all bits set").

## Serializing strings: `wputstr` / `wgetstr`

A string like `"hello"` does not fit in one word, so it must span multiple. `wputstr` packs characters into words using `memccpy`:

```
"hello\0"  →  [ h e l l | o \0 0 0 ]
               word 0      word 1
```

Each word is zero-initialized before copying, so unused trailing bytes are always zero. When `memccpy` finds the null terminator inside a word, that is the last word written. A NULL pointer is encoded as a single all-zero word.

`wgetstr` reads one word at a time and uses `memchr` to scan each word for a zero byte. When it finds one, it stops and returns a freshly allocated string containing all the accumulated bytes. It starts with a 128-word (1 KB) buffer and grows it as needed, so a string of any length round-trips.

## Serializing byte blobs: `wputdata` / `wgetdata`

A zero byte terminates a `wputstr` string, so that pair cannot carry data that *contains* zero bytes — and a decoded C string literal can: `"a\0c"` is three bytes, not one. `wputdata` therefore writes an explicit length first, then the raw bytes, zero-padded to the last word:

```
"a\0c"  →  [ 3 | a \0 c 0 0 0 0 0 ]
            len   word 0
```

`wgetdata` reads the length word, then exactly that many bytes' worth of words, and returns a freshly allocated buffer plus the byte count through its `size_t *len` argument (one extra NUL is appended, so the buffer is still safe to print when it happens to hold text). A zero length reads back as `NULL`, matching `wputstr`'s convention for the empty string. This is what the TAC exporter uses for string static initializers.

## Serializing floating-point: `wputd` / `wgetd`

Floating-point values cannot be cast to integers without corrupting their bits. The trick is to *reinterpret* the bytes using a `union`:

```c
union { double f; size_t w; } u;
u.f = 3.14;
wputw(u.w, stream);   // write raw bits as a word
```

Reading reverses it: read the word, stuff it into `u.w`, return `u.f`. On a 32-bit system where `double` is twice the size of `size_t`, the same trick uses `size_t w[2]` and writes two words.

## Position tracking: `wtell` / `wseek`

Because data lives in the buffer as well as on disk, a naive `lseek(fd, 0, SEEK_CUR)` gives the wrong answer. `wtell` corrects for this:

- **Read mode:** the OS file pointer has already advanced past buffered words that haven't been consumed yet, so `wtell` subtracts them.
- **Write mode:** the OS file pointer does not know about words sitting in the buffer waiting to be flushed, so `wtell` adds them.

The result is the word-precision logical position in the stream. `wseek` multiplies its word offset by `sizeof(size_t)` before calling `lseek`, so all arithmetic stays in word units.

## Error handling

There are two distinct error states, for two distinct situations:

| Flag | Cause | Detected by |
|---|---|---|
| `is_eof` | Stream ended cleanly at a word boundary | `weof()` |
| `is_error` | I/O failure, partial read, or wrong mode | `werror()` |

Both make `wgetw` return `(size_t)-1`, but callers can tell them apart. The AST importer uses a small helper that calls both checks and exits with a diagnostic message if either is true — no silently corrupted trees.

`wclearerr()` resets both flags, mostly useful in tests.

## Debug mode

Setting the global `wio_debug = 1` makes every `wputw`, `wgetw`, `wputstr`, and `wgetstr` print the value it just processed:

```
wputw 0x1         ← TAG_PROGRAM
wputw 0x2a        ← some enum value
wputstr 'main'    ← a function name
```

The AST export and import code have their own `export_debug` and `import_debug` flags that additionally log which serialization function is currently running. Together these flags let you trace the complete binary protocol without reaching for a hex dump.

## Two hand-offs, the same layer

### Hand-off 1: AST from `parse` to `lower`

**`parse` (writer side):**
```c
WFILE fd;
wdopen(&fd, fileno(stdout), "a");   // wrap stdout
wputw(TAG_PROGRAM, &fd);            // announce: "AST starts here"
for (ExternalDecl *d = program->decls; d; d = d->next)
    export_external_decl(&fd, d);   // recursively write each node
wputw(TAG_EOL, &fd);                // announce: "list ends here"
wclose(&fd);                        // flush
```

**`lower` (reader side):**
```c
WFILE input;
wdopen(&input, fileno(stdin), "r"); // wrap stdin
size_t tag = wgetw(&input);         // read first word
if (tag != TAG_PROGRAM) { /* fatal error */ }
// now read nodes until TAG_EOL
```

Every AST node is written as a tag word followed by its fields. Linked lists end with `TAG_EOL` instead of a null pointer. The entire tree becomes a flat, self-describing sequence of words that flows through a pipe without any intermediate file.

### Hand-off 2: TAC from `lower` to the code generator

After translating each declaration, `lower` writes TAC using the same `wio` primitives, but with a slightly different encoding convention.

AST nodes are identified by a type-tag (a single non-zero word that encodes the node kind). TAC uses a **presence marker** instead: a `0` word means "nothing here", a `1` word means "a node follows, and the next word is its kind". This is like a null-check built into the stream:

```c
// Writing an instruction (or the end of a list):
if (!instr) { wputw(0, out); return; }       // absent
wputw(1, out);                               // present
wputw((size_t)instr->kind, out);             // which instruction
// ... write its fields ...
export_instr(out, instr->next);              // recurse for the rest
```

The reader mirrors this exactly: read one word; if `0`, the list is done; if `1`, read the kind word, allocate the right struct, fill its fields, then recurse.

The TAC stream itself is wrapped in a header and trailer:

```c
tac_export_begin_stream(&tac_out);   // writes TAC_TAG_STREAM sentinel
// ... one tac_export_toplevel() call per declaration ...
tac_export_end_stream(&tac_out);     // writes 0 terminator and flushes
```

### Streaming: one declaration at a time

`lower` never holds the entire program in memory at once. Its main loop reads one AST declaration, translates it to TAC, writes the TAC, then frees both before moving to the next:

```c
for (;;) {
    ExternalDecl *ast = import_external_decl(&input);  // read one
    if (!ast) break;
    Tac_TopLevel *tac = translate(ast);                 // translate
    free_external_decl(ast);                            // done with AST
    tac_export_toplevel(&tac_out, tac);                 // write TAC
    tac_free_toplevel(tac);                             // done with TAC
}
```

Memory use stays proportional to the size of one declaration, not the whole program. The future code generator will follow the same pattern: read one `Tac_TopLevel` at a time, emit machine instructions for it, then move on.

---

That is `wio`: a thin, focused layer that gives the compiler a reliable, debuggable, portable way to serialize its internal data structures at every stage of the pipeline — one word at a time.
