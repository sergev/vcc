// Run a Braam process vcc built, without Braam: node run.mjs prog [args...]
//
// A fake kernel (backend/wasm/Plan.md §7.4).  It checks the module as braam-core's
// test/system/abi.mjs does (the imports, the five exports, one braam section), gives it
// env.memory of the section's page counts, writes argv through _alloc and calls _start.
// It serves kernel.sys (Exit, GetPid, Now, Random) and kernel.sys_async for Write (fd 1
// and 2 to stdout and stderr), Read (fd 0 from stdin, in 512-byte chunks), Open, Close,
// Read and Write on files under the current directory, and Sleep.  Each reply goes
// through _resume from a later macrotask, never from inside sys_async, so a step that
// forgets to return is caught.  The end reports "[exit N]" on stderr and exits with N;
// a trap reports itself and exits with 255, a process that waits on nothing with 254.
import { closeSync, openSync, readSync, writeSync } from 'node:fs';
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';

const PROC_MAGIC = 0x6d617262;
const PROC_ABI = 21;
const PROC_MAX_PAGES = 1600;
const SYS = { Exit: 1, GetPid: 2, Now: 3, Random: 5, Write: 16, Read: 17, Open: 18,
              Close: 19, Sleep: 32 };
const ERR = { Invalid: 1, NotFound: 3, Exists: 4, IsDir: 6, Perm: 7, Io: 8,
              Unsupported: 11 };
const O = { Read: 1, Write: 2, Create: 4, Trunc: 8, Append: 16, Excl: 32 };
const CHUNK = 512;
const READ_MAX = 65536 - 4;

function fail(why) {
    process.stderr.write(`run.mjs: ${why}\n`);
    process.exit(253);
}

if (process.argv.length < 3)
    fail('usage: node run.mjs prog [args...]');
const path = process.argv[2];
const module = new WebAssembly.Module(readFileSync(path));

// The process ABI, as abi.mjs asserts it.
const names = (list) => list.map((e) => `${e.module ? e.module + '.' : ''}${e.name}`).sort();
const imports = names(WebAssembly.Module.imports(module));
const exports = names(WebAssembly.Module.exports(module));
for (const name of imports)
    if (!['env.memory', 'kernel.sys', 'kernel.sys_async'].includes(name))
        fail(`imports ${name}, which is not the process ABI`);
if (!imports.includes('env.memory'))
    fail('does not import env.memory');
if (exports.join() !== '_alloc,_free,_resume,_sig,_start')
    fail(`exports [${exports}], expected [_alloc,_free,_resume,_sig,_start]`);
const meta = WebAssembly.Module.customSections(module, 'braam');
if (meta.length !== 1)
    fail(`carries ${meta.length} braam sections, expected 1`);
const m = new Uint32Array(meta[0].slice(0, 20));
if (m[0] !== PROC_MAGIC || m[1] !== PROC_ABI)
    fail(`metadata is ${m[0].toString(16)}/${m[1]}`);
if (m[4] !== PROC_MAX_PAGES)
    fail(`asks for ${m[4]} pages, expected ${PROC_MAX_PAGES}`);

const memory = new WebAssembly.Memory({ initial: m[3], maximum: m[4] });
const bytes = () => new Uint8Array(memory.buffer);
const start = Date.now();
let status = null;      // what Sys::Exit said
let finished = false;
let outstanding = 0;    // calls not answered yet
const files = new Map(); // descriptor -> node fd
let next_fd = 3;
let x;                  // the instance's exports

// A trap is a crash, as the kernel reports it; exit() makes one after Sys::Exit.
function trap(e) {
    const said = status === null ? '' : ` (Sys::Exit said ${status})`;
    process.stderr.write(`trap: ${e.message}${said}\n`);
    process.exit(255);
}

// After a step: 0 is an exited process.
function stepped(r) {
    if (r === 0) {
        finished = true;
        process.stderr.write(`[exit ${status}]\n`);
        process.exit(status & 255);
    }
}

// The reply to `token`: an i32 status, then `data`, in a block of the process's own.
function reply(token, st, data = new Uint8Array(0)) {
    outstanding++;
    setImmediate(() => {
        outstanding--;
        try {
            const n = 4 + data.length;
            const at = x._alloc(n);
            new DataView(memory.buffer).setInt32(at, st, true);
            bytes().set(data, at + 4);
            stepped(x._resume(token, at, n));
        } catch (e) {
            trap(e);
        }
    });
}

function nodeFlags(flags) {
    const rw = (flags & O.Read) && (flags & O.Write) ? 'r+' : 'r';
    if (!(flags & O.Write))
        return 'r';
    let f = flags & O.Append ? 'a' : flags & O.Trunc || flags & O.Create ? 'w' : rw;
    if (flags & O.Read && f !== 'r+')
        f += '+';
    if (flags & O.Excl)
        f += 'x';
    return f;
}

function errorOf(e) {
    switch (e.code) {
    case 'ENOENT': return -ERR.NotFound;
    case 'EEXIST': return -ERR.Exists;
    case 'EISDIR': return -ERR.IsDir;
    case 'EACCES': case 'EPERM': return -ERR.Perm;
    default: return -ERR.Io;
    }
}

const kernel = {
    sys(op, a0, a1, a2) {
        switch (op & 0xff) {
        case SYS.Exit: status = a0 | 0; return 0;
        case SYS.GetPid: return 1;
        case SYS.Now: return (Date.now() - start) | 0;
        case SYS.Random: return (Math.random() * 0x100000000) | 0;
        default: return -ERR.Unsupported;
        }
    },
    sys_async(op, token, ptr, len) {
        const code = op & 0xff, arg = op >>> 8;
        const payload = bytes().slice(ptr, ptr + len);
        const u32 = (at) => new DataView(payload.buffer).getUint32(at, true);
        switch (code) {
        case SYS.Write: {
            const fd = arg === 1 || arg === 2 ? arg : files.get(arg);
            if (fd === undefined)
                return reply(token, -ERR.Invalid);
            try {
                return reply(token, writeSync(fd, payload));
            } catch (e) {
                return reply(token, errorOf(e));
            }
        }
        case SYS.Read: {
            const fd = arg === 0 ? 0 : files.get(arg);
            if (fd === undefined)
                return reply(token, -ERR.Invalid);
            const max = Math.min(len >= 4 ? u32(0) : CHUNK, fd === 0 ? CHUNK : READ_MAX);
            const buf = new Uint8Array(max);
            try {
                const n = readSync(fd, buf, 0, max, null);
                return reply(token, 0, buf.subarray(0, n));
            } catch (e) {
                if (e.code === 'EAGAIN') // a terminal or pipe with nothing yet
                    return setTimeout(() => kernel.sys_async(op, token, ptr, len), 10);
                return reply(token, e.code === 'EOF' ? 0 : errorOf(e));
            }
        }
        case SYS.Open: {
            const name = new TextDecoder().decode(payload);
            try {
                const fd = openSync(name, nodeFlags(arg));
                files.set(next_fd, fd);
                return reply(token, next_fd++);
            } catch (e) {
                return reply(token, errorOf(e));
            }
        }
        case SYS.Close: {
            const fd = files.get(arg);
            if (fd === undefined)
                return reply(token, arg <= 2 ? 0 : -ERR.Invalid);
            files.delete(arg);
            closeSync(fd);
            return reply(token, 0);
        }
        case SYS.Sleep:
            outstanding++;
            return setTimeout(() => {
                outstanding--;
                reply(token, 0);
            }, len >= 4 ? u32(0) : 0);
        default:
            return reply(token, -ERR.Unsupported);
        }
    },
};

// A process that is waiting while nothing is coming will never finish.
process.on('beforeExit', () => {
    if (!finished) {
        process.stderr.write(`run.mjs: the process waits on nothing (${outstanding} calls)\n`);
        process.exit(254);
    }
});

try {
    x = new WebAssembly.Instance(module, { env: { memory }, kernel }).exports;
    // argv: u32 argc, then u32 length and the bytes of each word.
    const words = [basename(path), ...process.argv.slice(3)].map((w) => new TextEncoder().encode(w));
    const size = 4 + words.reduce((n, w) => n + 4 + w.length, 0);
    const at = x._alloc(size);
    const view = new DataView(memory.buffer);
    view.setUint32(at, words.length, true);
    let p = at + 4;
    for (const w of words) {
        view.setUint32(p, w.length, true);
        bytes().set(w, p + 4);
        p += 4 + w.length;
    }
    stepped(x._start(at, size));
} catch (e) {
    trap(e);
}
