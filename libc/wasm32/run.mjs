// Run a wasm32 program that vcc built: node run.mjs prog.wasm
//
// The host of libc/wasm32/console.s: env.putch writes a byte to standard output (buffered
// here, written out at the end and whenever the buffer fills), env.exit ends the program.
// _start runs it; a clean exit reports "[exit N]" on standard error and exits with N, a
// trap reports itself and exits with 255, without that line.
import { readFileSync, writeSync } from 'node:fs';

if (process.argv.length !== 3) {
    process.stderr.write('usage: node run.mjs prog.wasm\n');
    process.exit(2);
}

const out = new Uint8Array(4096);
let len = 0;
function flush() {
    if (len > 0)
        writeSync(1, out, 0, len);
    len = 0;
}

class Exit {
    constructor(status) {
        this.status = status;
    }
}

const env = {
    putch(c) {
        out[len++] = c;
        if (len === out.length)
            flush();
    },
    exit(status) {
        throw new Exit(status);
    },
};

let status = 0;
try {
    const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2]), { env });
    instance.exports._start();
} catch (e) {
    if (!(e instanceof Exit)) {
        flush();
        process.stderr.write(`trap: ${e.message}\n`);
        process.exit(255);
    }
    status = e.status;
}
flush();
process.stderr.write(`[exit ${status}]\n`);
process.exit(status & 255);
