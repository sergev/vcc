#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "abi.h"
#include "besm.h"
#include "frame.h"
#include "internal.h"
#include "xalloc.h"

//
// Peephole optimization on the BESM-6 backend instruction stream.
//
// The pass works on the `Besm_Instr` linked list a function's single `Besm_Block`
// holds, *after* instruction selection and *before* Madlen emission.  It is the
// backend's final polish: it removes the store/reload, mode-register, and
// compare/branch residue that one-TAC-node-at-a-time selection necessarily leaves
// behind.  See backend/besm6/Peephole_Rewrites.md for the theory and the worked before/after
// sequences, and backend/besm6/TODO.md (Phase M) for the rule catalogue.
//
// Currently implemented: the framework itself (this file), rule #27 (redundant reload
// elimination), rule #28 (dead temp-store elimination), rule #29 (NTR mode coalescing),
// rule #31 (branch / label cleanup), rule #32 (I/O address folding) and rule #33 (ω fixup
// before a conditional branch).  Rule #30 (compare → branch fusion) needs no
// dedicated code: it is the emergent product of #27 (which drops the boolean reload) and
// #28 (which drops the now-dead boolean store), made correct by the runtime helpers'
// logical-ω exit contract (see backend/besm6/Besm6_Runtime_Library.md, "ω mode and the AU mode
// register R") — and, where the value comes from the machine's own arithmetic rather than
// from a helper, by rule #33.  Rule #31's three control-flow rewrites — jump-to-next-label,
// unreachable tail (which also collapses the duplicate `uj b/ret`), and conditional-over-jump
// inversion — are not `rule_table` entries either: two need list look-ahead and one
// mutates the list, neither of which the `(cur, st)` predicate signature carries, so they
// are handled directly in the sweep.  Rule #32's two rewrites join them there for the same
// reason: each matches a fixed multi-instruction shape and rewrites it in place.  Rule #33
// is the one rewrite that *inserts* a node, which no predicate signature can express
// either, so it too lives in the sweep.
//
// Cutting across all of them is the *C group* (see `is_c_setter` below): a UTC or WTC and
// the instruction that follows it are one indivisible unit, because the C address-modifier
// register survives exactly one instruction.  The sweep therefore analyses and rewrites a
// group as a whole — a consumer's `(reg,addr)` fields do not name a frame slot, and deleting
// one while leaving its setter would re-bind C to whatever fell into the gap.  Rule #27 in
// consequence matches on a `Loc`, the location a group or a plain `xta`/`atx` addresses,
// rather than on a raw `(reg,off)` pair.  See backend/besm6/Peephole_Rewrites.md §5.9.
//

//
// The memory location a value lives in.
//
// Instruction selection reaches a location three ways, and the peephole must be able to
// tell them apart before it can say "A already holds this".  A plain `xta/atx reg,off`
// names a frame slot.  A `utc g` + bare `xta/atx` names a module-level global.  Anything
// else — a dereference, an address computation, a nonzero literal — is LOC_NONE: a real
// value, but at an address this pass cannot name, so it licenses no rewrite.
//
// A *zero* literal is the one constant that does name a location.  Instruction selection
// gives it no operand (see emit.c's attach_const), so it addresses mem[M[0] + 0] = mem[0],
// which the machine guarantees reads as zero.  It therefore classifies as LOC_FRAME(0, 0),
// a slot no `frame_lookup` can ever hand out — those carry r6 (REG_PAR) or r7 (REG_AUTO) —
// and one nothing ever stores to.  So `a_loc == LOC_FRAME(0,0)` holds exactly when A == 0,
// and rule #27 collapsing a second zero load onto the first is sound.
//
typedef enum {
    LOC_NONE,   // unnameable: no rewrite may match it
    LOC_FRAME,  // mem[M[reg] + off]           — a frame slot
    LOC_GLOBAL, // mem[&name + off]            — a module-level global
    LOC_DEREF,  // mem[mem[ptr]]               — through the pointer named below
} LocKind;

typedef struct {
    LocKind kind;
    int reg;          // LOC_FRAME: the index register (r6/r7);  LOC_DEREF: ditto, of the ptr
    int off;          // LOC_FRAME: slot number;  LOC_GLOBAL: word offset from `name`;
                      // LOC_DEREF: the pointer's slot number
    const char *name; // LOC_GLOBAL: the global's symbol;  LOC_DEREF: the global *pointer*'s
                      // symbol, or NULL when the pointer is the frame slot (reg, off).
                      // Borrowed from the group's UTC ->name; see the lifetime note below.
} Loc;

static Loc loc_none(void)
{
    Loc l = { LOC_NONE, 0, 0, NULL };
    return l;
}

// Do two locations denote the same word?  LOC_NONE never matches, not even itself.
//
// Two LOC_DEREFs through the same pointer denote the same word only if the pointer still
// holds the same value.  Nothing here checks that, and nothing needs to: the pointer lives
// in a frame slot or a global, so any write to it is an `atx`/`stx` that settles `a_loc`
// on the pointer itself (or on LOC_NONE), discarding the LOC_DEREF before it can be matched.
// See the state-invariant note on PeepState.
static bool loc_eq(Loc a, Loc b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case LOC_FRAME:
        return a.reg == b.reg && a.off == b.off;
    case LOC_GLOBAL:
        return a.off == b.off && strcmp(a.name, b.name) == 0;
    case LOC_DEREF:
        if ((a.name == NULL) != (b.name == NULL))
            return false;
        if (a.name != NULL)
            return strcmp(a.name, b.name) == 0;
        return a.reg == b.reg && a.off == b.off;
    default:
        return false;
    }
}

// True when `i` carries a symbolic or constant operand (a name — global/label/literal —
// or a structural constant), as opposed to a plain frame-slot memory operand `(reg,off)`.
// The state machine must not mistake a constant or global load for a frame slot, so every
// operand-classification test below uses this rather than a bare `name == NULL` check.
static bool has_operand_symbol(const Besm_Instr *i)
{
    return i->name != NULL || i->konst != NULL;
}

//
// The ω mode.
//
// UZA and U1A do not test a latched flag.  They test ω, which the hardware *recomputes at
// branch time* from the accumulator and the ω-mode field of the AU mode register R (bits
// 5–3) — see backend/besm6/Besm6_Instruction_Set.md §4.  The field records the group of the last
// instruction that set it, and it selects what the branch means:
//
//     logical         ω = (A ≠ 0)          uza branches when A = 0     ← what C wants
//     additive        ω = (A < 0)          uza branches when A ≥ 0
//     multiplicative  ω = (abs(A) < 0.5)   uza branches when not
//     none            ω = 1 always         uza never branches
//
// So a C truth test compiles correctly only if ω is *logical* when the branch executes.
// Instruction selection guarantees that by loading the condition with an `xta` (logical)
// immediately before the branch — but rules #27/#28 delete that load-store pair whenever A
// already holds the value, and the value's producer may well have left additive ω (`a-x`,
// from `if (x - y)`) or multiplicative ω (`arx`).  Rule #33 below restores it.
//
// An instruction either sets the field to one of the three groups, replaces the whole
// register (`ntr`/`xtr`), or leaves it alone; backend/besm6/Besm6_Instruction_Set.md records which
// for every opcode ("ω mode: Logical / Additive / Multiplicative / Kept / As set"), and
// `omega_after` below is that table transcribed.
//
typedef enum {
    OMEGA_UNKNOWN, // not known to be any of the below
    OMEGA_LOGICAL,
    OMEGA_ADDITIVE,
    OMEGA_MULT,
    OMEGA_NONE, // the empty group: ω = 1 unconditionally
    OMEGA_KEPT, // `omega_after` only: the instruction does not touch the field
} Omega;

// The ω group encoded in an R value: bits 5–3, one bit per group (mask 034).  `ntr 7` is
// `3 | 004` — the integer suppress bits plus the logical bit — which is why the runtime
// helpers' `R = 7` exit contract *is* the logical-ω contract.
static Omega omega_of_r(int r)
{
    switch (r & 034) {
    case 004:
        return OMEGA_LOGICAL;
    case 010:
        return OMEGA_MULT;
    case 020:
        return OMEGA_ADDITIVE;
    case 000:
        return OMEGA_NONE;
    default:
        return OMEGA_UNKNOWN; // more than one group bit: not a shape we model
    }
}

// Does this EXT/MOD address select a *read*?  On a read the AU switches to logical mode,
// because what arrives in A is a bit pattern rather than a number.  The selector bit is
// 04000 for `ext` and 0200 for `mod` (backend/besm6/Besm6_Instruction_Set.md, opcodes 033 and 002).
// Only a constant address in the instruction's own field can be classified; an address
// delivered through the C register (rule #32's trailer) is not visible here.
static bool io_reads(const Besm_Instr *i)
{
    if (has_operand_symbol(i) || i->reg != 0)
        return false;
    return (i->addr & (i->kind == BESM_IO_EXT ? 04000 : 0200)) != 0;
}

// The ω group in force after `i` executes, or OMEGA_KEPT when it leaves the field alone.
static Omega omega_after(const Besm_Instr *i)
{
    switch (i->kind) {
    // Logical: the loads and pushes, every bit-manipulation op, the shifts, and the
    // index-register transfers that pass through the accumulator.
    case BESM_MEM_XTA:
    case BESM_MEM_STX:
    case BESM_MEM_XTS:
    case BESM_MEM_ITA:
    case BESM_MEM_ITS:
    case BESM_MEM_STI:
    case BESM_LOG_AAX:
    case BESM_LOG_AOX:
    case BESM_LOG_AEX:
    case BESM_LOG_APX:
    case BESM_LOG_AUX:
    case BESM_LOG_ACX:
    case BESM_LOG_ANX:
    case BESM_EXP_SHIFTX:
    case BESM_EXP_SHIFTN:
    case BESM_EXP_GETR:
        return OMEGA_LOGICAL;

    // Additive: A+X, A-X, X-A, AMX, AVX.
    case BESM_ARITH_ADD:
    case BESM_ARITH_SUB:
    case BESM_ARITH_RSUB:
    case BESM_ARITH_ABSSUB:
    case BESM_ARITH_CNEG:
        return OMEGA_ADDITIVE;

    // Multiplicative: A*X, A/X, ARX and the exponent ops.
    case BESM_ARITH_MUL:
    case BESM_ARITH_DIV:
    case BESM_LOG_ARX:
    case BESM_EXP_EADDX:
    case BESM_EXP_ESUBX:
    case BESM_EXP_EADDN:
    case BESM_EXP_ESUBN:
        return OMEGA_MULT;

    // As set: the mode register is replaced outright.  `ntr n` carries its value in the
    // instruction; `xtr` takes it from memory, so the group is unknowable here.
    case BESM_EXP_SETR:
        return omega_of_r(i->addr);
    case BESM_EXP_SETRMEM:
        return OMEGA_UNKNOWN;

    // A call returns with logical ω: every runtime helper exits that way by contract (see
    // backend/besm6/Besm6_Runtime_Library.md, "ω mode and the AU mode register R"), and a compiled
    // C function returns through b/ret, whose last accumulator ops are `stx`/`sti`.
    case BESM_BRANCH_CALL:
    case BESM_BRANCH_VJM:
        return OMEGA_LOGICAL;

    // An extracode's handler sets logical ω on return.  EXT/MOD do so on a read address.
    case BESM_IO_EXTRACODE:
        return OMEGA_LOGICAL;
    case BESM_IO_EXT:
    case BESM_IO_MOD:
        return io_reads(i) ? OMEGA_LOGICAL : OMEGA_KEPT;

    // A label may be reached from anywhere, and a `stop` hands the machine to the operator.
    case BESM_STMT_LABEL:
    case BESM_STMT_NAME:
    case BESM_STMT_BASE:
    case BESM_STMT_SUBP:
    case BESM_STMT_ENTRY:
    case BESM_STMT_END:
    case BESM_BRANCH_STOP:
        return OMEGA_UNKNOWN;

    // Kept: the stores, the index-register ops, the C-register setters and every branch
    // (ATX ATI MTJ J+M UTC WTC VTM UTM UJ VZM V1M VLM UZA U1A).
    default:
        return OMEGA_KEPT;
    }
}

//
// Tracked implicit machine state, valid only along straight-line code.
//
// A value in A is described by the location it mirrors.  Most rewrites are licensed by
// knowing "A currently holds location L".  The mode register R is tracked as a whole (for
// NTR mode coalescing, rule #29) and its ω-group field separately (for rule #33).  The two
// are deliberately independent: `r_val` is only believed while `r_known`, which only an
// `ntr` sets, whereas every ALU op rewrites the ω group without disturbing the suppress
// bits rule #29 cares about — so an arithmetic instruction invalidates `omega`'s successor
// but not `r_val`'s.
//
// `a_loc` needs no memory-clobber analysis, which is worth spelling out because it looks
// like it should.  On this machine memory is only ever written *from A* (`atx`, `stx`), so a
// store can never falsify "A mirrors L": either it writes somewhere other than L, or it
// writes L with the value A already holds.  A's mirror can therefore only go stale when A
// itself changes, or when the frame base M[6]/M[7] moves — and `state_step` settles `a_loc`
// at every instruction that does either (conservatively, to LOC_NONE unless it is an
// `xta`/`atx` naming a location).  Only UTC and WTC leave `a_loc` untouched, and they touch
// neither A nor memory.
//
// A LOC_DEREF additionally depends on the pointer's value.  That falls out of the same
// invariant: the pointer is itself in memory, so it can only be written from A, by an
// `atx`/`stx` that settles `a_loc` on the pointer's own location or on LOC_NONE — either
// way discarding the LOC_DEREF.  A CALL, which may write anything, is a block boundary.
//
// `Loc.name` borrows the `->name` of the group's UTC.  Deletion always removes a whole group
// at the cursor, and `a_loc` is only ever read to match a *later* group, so the instruction
// a tracked name points into always outlives the state that names it.
//
typedef struct {
    Loc a_loc;    // the location A currently mirrors (LOC_NONE: unknown)
    bool r_known; // true: r_val is the current mode register R
    int r_val;
    Omega omega;         // the ω group a conditional branch here would test
    bool in_unreachable; // true: we are past an unconditional transfer (uj/stop),
                         // before the next label or structural directive (rule #31)
} PeepState;

// Reset all tracked state — used at every basic-block boundary.
static void state_reset(PeepState *st)
{
    st->a_loc          = loc_none();
    st->r_known        = false;
    st->omega          = OMEGA_UNKNOWN;
    st->in_unreachable = false;
}

// Fold one instruction's effect into the tracked ω group.  Split out of `state_step`
// because the sweep also needs it for the boundaries it resets across (a CALL leaves
// logical ω; a read-address `ext` does too) and for a C group's consumer.
static void omega_step(PeepState *st, const Besm_Instr *i)
{
    Omega w = omega_after(i);
    if (w != OMEGA_KEPT)
        st->omega = w;
}

//
// C groups.
//
// The C address-modifier register is reset to zero after *every* instruction except UTC
// (022) and WTC (023).  A C-setter and the single instruction that follows it therefore
// form an atomic pair: the follower's effective address is `addr + M[reg] + C`, and nothing
// may be inserted between them or deleted from between them.  The backend emits three
// shapes (see emit.c): `utc name` + consumer (a global's own word), `wtc reg,off` + consumer
// (through a frame-resident pointer) and `wtc name` + consumer (through a global pointer).
// A group of more than two nodes is still possible in principle — WTC is at once a consumer
// and a setter — so the walk below chains setters rather than assuming a pair.
//
// Two consequences for this pass.  A bare `xta`/`atx` (reg 0, addr 0) after a setter is *not*
// a frame-slot access: it reads or writes mem[C], an address the tracked state has no name
// for.  And no rewrite may delete a consumer while leaving its setter, which would silently
// re-bind C to whatever instruction fell into the gap.
//
static bool is_c_setter(const Besm_Instr *i)
{
    return i->kind == BESM_MOD_UTC || i->kind == BESM_MOD_WTC;
}

// Walk the setter chain starting at `first` and return the single instruction that consumes
// C, or NULL if the block ends on a setter.  `*count` gets the number of nodes in the whole
// group (setters plus consumer), which is what a deletion must splice out.
static Besm_Instr *c_group_consumer(Besm_Instr *first, int *count)
{
    int n            = 0;
    Besm_Instr *i    = first;
    while (i != NULL && is_c_setter(i)) {
        n++;
        i = i->next;
    }
    *count = (i != NULL) ? n + 1 : n;
    return i;
}

// The memory location a C group addresses.  Three shapes name one (all emitted by emit.c):
//
//   `utc name` + `xta/atx 0,woff`      LOC_GLOBAL(name, addr + woff)   emit_xta_val etc.
//   `wtc reg,off` + `xta/atx`          LOC_DEREF via frame pointer     emit_wtc_ptr, local
//   `wtc name` + `xta/atx`             LOC_DEREF via global pointer    emit_wtc_ptr, global
//
// Everything else is LOC_NONE:
//
//   `utc reg,off`      — address arithmetic (emit_member_fatptr, GET_ADDRESS_DECAY, the
//                        prologue's `utc 14,1`); C holds an address, not a location's name
//   any other consumer — `xts`, `asx`, arithmetic, `vjm`, `vtm`: not a plain word access
//
// A `utc` whose name is a Madlen constant literal (`,utc, =i1`) also classifies as
// LOC_GLOBAL, which is sound: a constant-pool entry has a fixed address like any global.
static Loc c_group_loc(const Besm_Instr *first)
{
    Loc l               = loc_none();
    const Besm_Instr *c = first->next; // provisional consumer

    if (first->kind == BESM_MOD_UTC) {
        // A `utc` names a location only when it carries a symbol and no index register:
        // C = &name + addr.  `utc reg,off` computes an address instead.
        if (first->name == NULL || first->konst != NULL || first->reg != 0)
            return loc_none();
        if (c == NULL || is_c_setter(c))
            return loc_none();
        l.kind = LOC_GLOBAL;
        l.name = first->name;
        l.off  = first->addr; // the consumer's own offset is added below
    } else {                  // BESM_MOD_WTC: C = the pointer's contents — a dereference
        if (first->konst != NULL)
            return loc_none();
        if (first->name != NULL) {
            // `wtc name`: through the module-level pointer `name`.  An index register
            // would make the pointer's own word a computed address we cannot name.
            if (first->reg != 0 || first->addr != 0)
                return loc_none();
            l.kind = LOC_DEREF;
            l.name = first->name;
        } else { // `wtc reg,off`: through the frame-resident pointer in that slot
            l.kind = LOC_DEREF;
            l.reg  = (int)first->reg;
            l.off  = first->addr;
        }
    }

    // The consumer must be a bare word access through C: `xta`/`atx` with EA = C + addr.
    if (c == NULL || (c->kind != BESM_MEM_XTA && c->kind != BESM_MEM_ATX) || c->reg != 0 ||
        has_operand_symbol(c))
        return loc_none();
    if (l.kind == LOC_GLOBAL)
        l.off += c->addr;
    else if (c->addr != 0)
        return loc_none(); // an offset off a dereferenced pointer: not a shape we model
    return l;
}

// The location a non-group `xta`/`atx` addresses: its frame slot, unless it carries a
// symbolic or constant operand (a global load emits its own UTC; a nonzero literal names no
// slot).  An operandless `xta` reaching here is a zero load and yields LOC_FRAME(0, 0) —
// mem[0] — never a real slot; a bare `xta`/`atx` that reads mem[C] belongs to a C group and
// is stepped by the sweep, so it never reaches this function.
static Loc plain_loc(const Besm_Instr *i)
{
    if ((i->kind != BESM_MEM_XTA && i->kind != BESM_MEM_ATX) || has_operand_symbol(i))
        return loc_none();
    Loc l = { LOC_FRAME, (int)i->reg, i->addr, NULL };
    return l;
}

//
// Does this instruction end a basic block?  A label can be re-entered from
// elsewhere and a branch transfers control (a CALL also clobbers A), so tracked
// state cannot be assumed to survive past it.  The non-dataflow assembler
// directives (NAME/SUBP/BASE/ENTRY/END and the data pseudo-ops) likewise carry no
// straight-line machine state, so they reset too.  A STOP does not transfer control
// — execution resumes at the next instruction when the operator presses continue —
// but the operator may have altered the registers from the console, so the tracked
// state is dropped across it too.
//
// The privileged I/O instructions (EXT/MOD) transfer no control either, but they rewrite
// the tracked state behind the pass's back: each leaves A holding a device word, and on a
// *read* address (bit 04000 for EXT, 0200 for MOD) the hardware switches the AU mode
// register R to logical — the very register rules #29(a)/(b) track.  So they reset too.
//
static bool is_block_boundary(const Besm_Instr *i)
{
    switch (i->kind) {
    case BESM_BRANCH_UZA:
    case BESM_BRANCH_U1A:
    case BESM_BRANCH_UJ:
    case BESM_BRANCH_VJM:
    case BESM_BRANCH_VZM:
    case BESM_BRANCH_V1M:
    case BESM_BRANCH_VLM:
    case BESM_BRANCH_CALL:
    case BESM_BRANCH_STOP:
    case BESM_IO_EXT:
    case BESM_IO_MOD:
    // An extracode traps into the operating system, which runs arbitrary code before
    // returning: A, the mode register R and ω are all unknown afterwards (and so is r14,
    // which the hardware loads from the effective address).
    case BESM_IO_EXTRACODE:
    case BESM_STMT_LABEL:
    case BESM_STMT_NAME:
    case BESM_STMT_BASE:
    case BESM_STMT_SUBP:
    case BESM_STMT_ENTRY:
    case BESM_STMT_END:
        return true;
    default:
        return false;
    }
}

//
// Update the tracked state to reflect executing a single non-group instruction `i` in
// straight-line code.  This runs only for non-boundary instructions (boundaries reset the
// state instead); C groups are stepped by the sweep, which knows their effective location.
//
// XTA and ATX of a frame slot (no symbolic name) both leave A mirroring that slot.
// Every other instruction may clobber A, so conservatively mark A unknown; later
// rules refine this.
//
static void state_step(PeepState *st, const Besm_Instr *i)
{
    // Mode register R: `ntr` (SETR) sets it to its operand.  Nothing else changes R
    // in straight-line code — a CALL is a block boundary, handled by the sweep.
    if (i->kind == BESM_EXP_SETR) {
        st->r_known = true;
        st->r_val   = i->addr;
    }
    // The ω group, for rule #33.  Independent of `r_known`: an ALU op rewrites the group
    // without making the rest of R unknown, and vice versa.
    omega_step(st, i);
    switch (i->kind) {
    case BESM_MEM_XTA:
    case BESM_MEM_ATX:
        st->a_loc = plain_loc(i);
        return;
    default:
        st->a_loc = loc_none();
        return;
    }
}

//
// Rule #27 — redundant reload elimination.  Section 5.1 of backend/besm6/Peephole_Rewrites.md.
//
// `cur` is an `xta` reload of a location whose value the tracked state says A already holds
// (the preceding `atx` to it stored the value and did not disturb A).  The reload is pure
// waste; report a match so the caller splices it out.  For a plain frame slot the reload is
// the single `xta`; for a global it is the whole `utc name` + `xta` group, which the sweep
// matches through `c_group_loc` instead of this predicate.  A volatile reload stays: the
// access is made as it was written.
//
static bool rule_redundant_reload(const Besm_Instr *cur, const PeepState *st)
{
    return cur->kind == BESM_MEM_XTA && !cur->is_volatile && loc_eq(plain_loc(cur), st->a_loc);
}

//
// Rule #29(a) — redundant NTR elimination.  See backend/besm6/Peephole_Rewrites.md §5.3.
//
// `cur` is an `ntr n` (SETR) that re-establishes a mode-register value R already
// holds (the tracked state says R == n), so it has no effect; report a match so the
// caller deletes it.  This drops the leading `ntr 0` of an FP op when R is already 0,
// and a trailing `ntr 7` when R is already 7 (e.g. straight after `b/save`).
//
static bool rule_redundant_ntr(const Besm_Instr *cur, const PeepState *st)
{
    return cur->kind == BESM_EXP_SETR && st->r_known && cur->addr == st->r_val;
}

//
// Rule table.  Each rule inspects the cursor instruction and the tracked state and
// returns true when the cursor should be deleted.  (Rule #30, compare → branch fusion,
// needs no entry — it is realized by #27 + #28; see the file header.)  Rules #31–#32
// would append here.
//
// Rule #28 (dead temp-store elimination) is *not* a table entry: deciding whether an
// `atx` is dead needs forward look-ahead within the block plus the frame's temp-slot
// classification, neither of which the `(cur, st)` predicate signature carries.  It is
// handled by `dead_temp_store` directly in the sweep below.
//
typedef bool (*PeepRule)(const Besm_Instr *cur, const PeepState *st);

static const PeepRule rule_table[] = {
    rule_redundant_reload,
    rule_redundant_ntr,
};
#define NUM_RULES (sizeof(rule_table) / sizeof(rule_table[0]))

//
// Rule #28 — dead temp-store elimination.  See backend/besm6/Peephole_Rewrites.md §5.2.
//
// An `atx reg,off` that stores a compiler temporary's frame slot is dead when nothing
// reads that slot before it is overwritten or the basic block ends.  Restricting to
// '%'-temporaries is essential: temporaries are never address-taken, so every read of
// the slot appears as a direct `(reg,off)` operand a forward scan can see, whereas a
// named local could be read through a pointer (an aliased access invisible to the scan).
//

// Does `i` reference auto slot `off` (r7 + off) as a direct memory *read* operand?  The
// `reg`/`addr` fields are overloaded (index-register ops put an index register in `reg`
// or an immediate in `addr`), so only the kinds that take a frame-slot memory operand
// and load from it count.  WTC counts: a word/byte dereference reads the pointer slot with
// `wtc reg,off` (it copies bits 15:1 into the C register), so a store that materialised an
// ADD_PTR address into that slot is live and must not be dropped as a dead temp.
static bool instr_reads_auto_slot(const Besm_Instr *i, int off)
{
    if (has_operand_symbol(i) || (int)i->reg != REG_AUTO || i->addr != off)
        return false;
    switch (i->kind) {
    case BESM_MOD_WTC:
    case BESM_MEM_XTA:
    case BESM_MEM_XTS:
    case BESM_ARITH_ADD:
    case BESM_ARITH_SUB:
    case BESM_ARITH_RSUB:
    case BESM_ARITH_ABSSUB:
    case BESM_ARITH_MUL:
    case BESM_ARITH_DIV:
    case BESM_ARITH_CNEG:
    case BESM_LOG_AAX:
    case BESM_LOG_AOX:
    case BESM_LOG_AEX:
    case BESM_LOG_ARX:
    case BESM_LOG_APX:
    case BESM_LOG_AUX:
    case BESM_LOG_ACX:
    case BESM_LOG_ANX:
    case BESM_EXP_EADDX:
    case BESM_EXP_ESUBX:
    case BESM_EXP_SHIFTX:
    case BESM_EXP_SETRMEM:
    // EXT/MOD/EXTRACODE reach no memory word — their effective address *is* the device
    // register or the extracode's argument, and instruction selection never gives them a
    // frame-slot operand (their modifier register is 0 or the scratch r14).  They are listed
    // only so the whitelist stays complete: its `default: return false` means an operand kind
    // left out would license rule #28 to delete the store that feeds it.
    case BESM_IO_EXT:
    case BESM_IO_MOD:
    case BESM_IO_EXTRACODE:
        return true;
    default:
        return false;
    }
}

// Does `i` *write* auto slot `off` (r7 + off)?  `atx` stores A; `stx` stores A and pops.
static bool instr_writes_auto_slot(const Besm_Instr *i, int off)
{
    if (has_operand_symbol(i) || (int)i->reg != REG_AUTO || i->addr != off)
        return false;
    return i->kind == BESM_MEM_ATX || i->kind == BESM_MEM_STX;
}

//
// `cur` is an `atx` to a temporary's auto slot.  Scan forward within the current basic
// block: if the slot is read before being overwritten, the store is live; if overwritten
// first, the store is dead; if neither happens before the block boundary, the store is
// dead only when the temporary lives in a single basic block (so it cannot be live-out).
//
static bool dead_temp_store(const Besm_Instr *cur, const Frame *frame, const bool *multiblock)
{
    if (cur->kind != BESM_MEM_ATX || has_operand_symbol(cur) || (int)cur->reg != REG_AUTO)
        return false;
    if (!frame || !frame_slot_is_temp(frame, REG_AUTO, cur->addr))
        return false;

    int off = cur->addr;
    for (const Besm_Instr *j = cur->next; j && !is_block_boundary(j); j = j->next) {
        if (instr_reads_auto_slot(j, off))
            return false; // value is used: store is live
        if (instr_writes_auto_slot(j, off))
            return true; // overwritten before any read: store is dead
    }
    // Block ends with no further read.  Dead iff the temporary is confined to one block.
    return multiblock == NULL || !multiblock[off];
}

//
// Rule #29(b) — dead NTR elimination.  See backend/besm6/Peephole_Rewrites.md §5.3.
//
// Two FP ops in a row emit `ntr 7` (restore) immediately chased by `ntr 0` (re-enter
// FP mode), with only R-independent moves between them.  The first `ntr` is dead: its
// R value is overwritten before anything reads it.  Together with rule #29(a) (which
// then drops the redundant re-set), R stays 0 across the run and is restored once at
// the end.
//

// Is `i`'s result independent of the mode register R?  These are the data-movement and
// index-register ops whose behavior does not depend on R, so they may sit between a
// dead `ntr` and the `ntr` that overwrites it.  Everything else — additive/
// multiplicative arithmetic (A±X / A*X / A/X), exponent and shift ops, etc. — may
// depend on R and therefore ends the scan, keeping the `ntr` live.
static bool instr_is_r_independent(const Besm_Instr *i)
{
    switch (i->kind) {
    case BESM_MEM_XTA:
    case BESM_MEM_ATX:
    case BESM_MEM_STX:
    case BESM_MEM_XTS:
    case BESM_MEM_ITA:
    case BESM_MEM_ATI:
    case BESM_MEM_ITS:
    case BESM_MEM_STI:
    case BESM_MEM_MTJ:
    case BESM_REG_VTM:
    case BESM_REG_UTM:
    case BESM_REG_JADDM:
    case BESM_MOD_UTC:
    case BESM_MOD_WTC:
        return true;
    default:
        return false;
    }
}

//
// `cur` is an `ntr` (SETR).  Scan forward within the basic block: if a later `ntr`
// overwrites R before any R-dependent instruction reads it, `cur` is dead.  If an
// R-dependent instruction or the block boundary comes first, `cur` is live (the
// boundary case is the "restore R once at the end" that must survive).
//
static bool dead_ntr_set(const Besm_Instr *cur)
{
    if (cur->kind != BESM_EXP_SETR)
        return false;
    for (const Besm_Instr *j = cur->next; j && !is_block_boundary(j); j = j->next) {
        if (j->kind == BESM_EXP_SETR)
            return true; // R overwritten before any use: store is dead
        if (!instr_is_r_independent(j))
            return false; // R is used: store is live
    }
    return false; // block ends with no overwrite: keep (restore-at-end)
}

// Splice a single node out of a block's list and free it (defined below).
static void delete_instr(Besm_Block *block, Besm_Instr *prev, Besm_Instr *cur);

//
// Rule #31 — branch / label cleanup.  See backend/besm6/Peephole_Rewrites.md §5.5.
//
// Three rewrites that need only the control-flow shape, not the tracked A/R/ω state.
// None fits the `(cur, st)` `PeepRule` signature: two need list look-ahead and one
// mutates the list, so they are handled directly in the sweep.
//

// Is `i` an instruction the unreachable-tail rule may delete?  A label can be re-entered
// from elsewhere and the structural directives (NAME/SUBP/BASE/ENTRY/END) and data
// pseudo-ops carry no control flow, so they terminate the unreachable run and are never
// deleted; every real instruction after an unconditional transfer is dead and removable.
static bool unreachable_deletable(const Besm_Instr *i)
{
    switch (i->kind) {
    case BESM_STMT_LABEL:
    case BESM_STMT_NAME:
    case BESM_STMT_BASE:
    case BESM_STMT_SUBP:
    case BESM_STMT_ENTRY:
    case BESM_STMT_END:
    case BESM_DATA_INT:
    case BESM_DATA_REAL:
    case BESM_DATA_LOG:
    case BESM_DATA_BSS:
    case BESM_DATA_EQU:
    case BESM_DATA_REF:
    case BESM_DATA_STRING:
    case BESM_DATA_Z00:
        return false;
    default:
        return true;
    }
}

// Rule #31(a) — jump to the next instruction.  A `uj L` immediately followed by the
// definition of label `L` is a no-op fall-through; report a match so the caller deletes
// the `uj`.
static bool jump_to_next_label(const Besm_Instr *cur)
{
    return cur->kind == BESM_BRANCH_UJ && cur->name != NULL && cur->next != NULL &&
           cur->next->kind == BESM_STMT_LABEL && cur->next->name != NULL &&
           strcmp(cur->name, cur->next->name) == 0;
}

// Rule #31(c) — invert a conditional that only skips an unconditional jump.
//
// `uza L` / `uj M` / `L:`  ⇒  `u1a M` / `L:`   (and symmetrically `u1a`⇒`uza`).
// The conditional skips the `uj` exactly when it would NOT branch, so flipping the
// condition and retargeting it to M is equivalent, and the now-dead `uj` is removed.
// The label `L:` is left in place — it may still be a target elsewhere, and an
// unreferenced label is harmless.  Mutates `cur` in place, deletes the `uj` node, and
// returns true on a match.
static bool try_invert_branch_over_jump(Besm_Block *block, Besm_Instr *cur)
{
    if (cur->kind != BESM_BRANCH_UZA && cur->kind != BESM_BRANCH_U1A)
        return false;
    Besm_Instr *uj  = cur->next;
    if (uj == NULL || uj->kind != BESM_BRANCH_UJ || uj->name == NULL)
        return false;
    const Besm_Instr *lbl = uj->next;
    if (lbl == NULL || lbl->kind != BESM_STMT_LABEL || lbl->name == NULL)
        return false;
    if (cur->name == NULL || strcmp(cur->name, lbl->name) != 0)
        return false;

    cur->kind = (cur->kind == BESM_BRANCH_UZA) ? BESM_BRANCH_U1A : BESM_BRANCH_UZA;
    xfree(cur->name);
    cur->name = xstrdup(uj->name);
    delete_instr(block, cur, uj); // unlink `uj` (cur is its predecessor) and free it
    return true;
}

//
// Rule #32 — I/O address folding.  See backend/besm6/Peephole_Rewrites.md §5.10.
//
// `ext`, `mod` and the extracode name their device register / trap argument through the
// effective address, `EA = (addr + M[reg] + C) mod 0100000`.  For anything but a small
// constant, instruction selection (emit_io_op in intrinsics.c) delivers that address through
// the stack:
//
//     xta <addr>          A = the effective address
//     [utc g]  xts <acc>  push it; A = the accumulator operand
//  15 wtc                 stack mode: pop it back into C
//     ext                 EA = C
//
// The round-trip is a fixed, recognizable trailer, and two rewrites shorten what feeds it.
// Both are pure look-ahead on the list shape, so like rule #31 they live in the sweep rather
// than in `rule_table`; neither inserts a node — each deletes its anchor and mutates the
// trailer in place.  Their guard is that only these three instruction kinds ever carry this
// trailer, so no ordinary code can match.
//

static bool is_io_kind(const Besm_Instr *i)
{
    return i->kind == BESM_IO_EXT || i->kind == BESM_IO_MOD || i->kind == BESM_IO_EXTRACODE;
}

// Match the trailer above starting at `i`, and hand back its three nodes.  The `utc name`
// escape is the one emit_xts_val emits when the *accumulator* operand is a global; its C is
// consumed by the `xts`, well before the `wtc` sets C again for the I/O op.
static Besm_Instr *io_stack_trailer(Besm_Instr *i, Besm_Instr **out_xts, Besm_Instr **out_wtc)
{
    if (i != NULL && i->kind == BESM_MOD_UTC && i->name != NULL && i->konst == NULL && i->reg == 0)
        i = i->next;
    if (i == NULL || i->kind != BESM_MEM_XTS)
        return NULL;
    Besm_Instr *wtc = i->next;
    if (wtc == NULL || wtc->kind != BESM_MOD_WTC || (int)wtc->reg != REG_SP || wtc->addr != 0 ||
        has_operand_symbol(wtc))
        return NULL;
    Besm_Instr *io = wtc->next;
    if (io == NULL || !is_io_kind(io) || io->reg != 0)
        return NULL;
    *out_xts = i;
    *out_wtc = wtc;
    return io;
}

// The value of `i`'s constant operand as a displacement that fits the Format-1 address
// field, or -1 when it has none, is real, or is too large.  A literal operand is required:
// an instruction with no operand at all is the zero constant (see emit.c's attach_const),
// but adding zero never survives TAC constant folding, so a bare instruction here is a
// memory operand — a frame slot, or mem[C] inside a C group — and names no displacement.
static int displacement_operand(const Besm_Instr *i)
{
    if (i->name != NULL || i->konst == NULL)
        return -1;
    Besm_ConstWord w = besm_const_word(i->konst);
    if (w.is_real || w.word > BESM_SHORT_ADDR_MAX)
        return -1;
    return (int)w.word;
}

//
// Rule #32(a) — displacement fusion.  A constant added to the address just before the
// trailer belongs in the instruction's own address field instead:
//
//   6 xta        6 xta            6 xta          6 xta
//     a+x =100     xts       ⇒      xts =1         xts
//     xts       15 wtc              call b$uadd 15 wtc
//  15 wtc          ext 64        15 wtc            ext 1
//     ext                           ext
//
// Two shapes reach here because C picks the addition by the operand type.  A signed one is
// the machine's own `a+x`; an unsigned one is a call to the runtime helper `b$uadd`, which
// pops the pushed left operand and leaves the sum in A (see libc/besm6/b_uadd) — deleting
// the `xts`+`call` pair leaves the base in A and the stack balanced exactly as before.
//
// Folding is exact in both cases even though the addition is 48-bit and the address field
// is 15: the trailer's `wtc` keeps only bits 15:1 and EA is formed mod 0100000, and
// truncation commutes with addition, so `low15(base + N)` and `low15(base) + N mod 2^15`
// are the same address.  Overflow of the C-level sum therefore cannot change the outcome.
//
static int try_io_displacement_fusion(Besm_Instr *cur)
{
    Besm_Instr *after = NULL; // the trailer's first node
    int nodes         = 0;    // how many nodes at `cur` the fold removes

    if (cur->kind == BESM_ARITH_ADD && cur->reg == 0) {
        after = cur->next;
        nodes = 1;
    } else if (cur->kind == BESM_MEM_XTS && cur->next != NULL &&
               cur->next->kind == BESM_BRANCH_CALL && cur->next->name != NULL &&
               strcmp(cur->next->name, "b$uadd") == 0) {
        after = cur->next->next;
        nodes = 2;
    } else {
        return 0;
    }

    int disp = displacement_operand(cur);
    if (disp < 0)
        return 0;

    Besm_Instr *xts, *wtc;
    Besm_Instr *io = io_stack_trailer(after, &xts, &wtc);
    if (io == NULL || io->addr != 0)
        return 0;

    io->addr = disp;
    return nodes;
}

//
// Rule #32(b) — memory-resident address.  When the address was merely loaded out of a frame
// slot or a global, the push/pop round-trip is pure waste: `wtc` reads memory itself, so it
// can address that location directly and the load disappears.
//
//   6 xta              xta          utc g            xta
//     xts       ⇒    6 wtc            xta      ⇒     wtc g
//  15 wtc              ext 1          xts            ext 1
//     ext 1                        15 wtc
//                                     ext 1
//
// The `xts` becomes the plain accumulator load it always was, and the `wtc` moves off the
// stack pointer onto the location itself — one instruction either way, since WTC is Format 2
// and its own 15-bit address field reaches any global directly.  A's previous value is dead
// across the rewrite: the `xts`-turned-`xta` redefines it unconditionally before the I/O op,
// which is itself a basic-block boundary.
//
static int try_io_memory_address(Besm_Instr *cur)
{
    Loc base;
    Besm_Instr *after; // the trailer's first node
    int nodes;         // how many nodes at `cur` the fold removes

    if (cur->kind == BESM_MEM_XTA && !has_operand_symbol(cur) && cur->reg != 0) {
        base  = plain_loc(cur);
        after = cur->next;
        nodes = 1;
    } else if (cur->kind == BESM_MOD_UTC) {
        base = c_group_loc(cur);
        if (base.kind != LOC_GLOBAL || base.off != 0 || cur->next == NULL ||
            cur->next->kind != BESM_MEM_XTA)
            return 0;
        after = cur->next->next;
        nodes = 2;
    } else {
        return 0;
    }
    if (base.kind != LOC_FRAME && base.kind != LOC_GLOBAL)
        return 0;

    Besm_Instr *xts, *wtc;
    if (io_stack_trailer(after, &xts, &wtc) == NULL)
        return 0;

    xts->kind = BESM_MEM_XTA;
    wtc->reg  = 0;
    wtc->addr = 0;
    if (base.kind == LOC_FRAME) {
        wtc->reg  = (unsigned)base.reg;
        wtc->addr = base.off;
    } else {
        wtc->name = xstrdup(base.name); // borrowed from `cur`, which is about to be freed
    }
    return nodes;
}

//
// Rule #33 — ω fixup before a conditional branch.  See backend/besm6/Peephole_Rewrites.md §5.11.
//
// `uza`/`u1a` test ω, and ω means "A = 0?" only under the logical group (see the ω section
// at the top of this file).  Instruction selection always loads the condition with an `xta`
// — logical — right before the branch, but rule #27 deletes that reload whenever A already
// holds the value, exposing whatever group its producer left:
//
//     6 xta            6 xta         6 xta
//     6 a-x 1          6 a-x 1       6 a-x 1
//       atx %0     ⇒     uza .T1  ⇒    aex
//       xta %0                         uza .T1
//       uza .T1        ^ #27 + #28 leave additive ω: a *sign* test
//
// The repair is one instruction: `aex` with no operand XORs memory word 0 — architecturally
// zero — into A.  A is unchanged, the R suppress bits are unchanged, and the ω group
// becomes logical.  AEX is the cheapest of the logical ops; the runtime library writes the
// same no-op as `,aox,` (backend/besm6/Besm6_Runtime_Library.md, "ω mode and the AU mode register R").
//
// It is inserted only where the tracked group is not already logical, so the common cases —
// a surviving `xta`, a relational runtime helper (rule #30's fusion), a read-address `ext` —
// pay nothing.  This is also what lets `arx` and any future inlined multiply feed an `if`:
// they leave multiplicative ω, and the fixup covers all three groups, not just additive.
//
static bool needs_omega_fixup(const Besm_Instr *cur, const PeepState *st)
{
    if (cur->kind != BESM_BRANCH_UZA && cur->kind != BESM_BRANCH_U1A)
        return false;
    return st->omega != OMEGA_LOGICAL;
}

// Splice `node` into a block's list immediately before `cur`, whose predecessor is `prev`.
// The caller must advance its own cursor bookkeeping (`prev` becomes `node`).
static void insert_before(Besm_Block *block, Besm_Instr *prev, Besm_Instr *cur, Besm_Instr *node)
{
    node->next = cur;
    if (prev)
        prev->next = node;
    else
        block->body = node;
}

// If `i` directly reads or writes an auto slot, report its offset.  Used to attribute
// each slot reference to the basic block it occurs in (multi-block analysis).
static bool instr_auto_slot_ref(const Besm_Instr *i, int *off)
{
    if (has_operand_symbol(i) || (int)i->reg != REG_AUTO)
        return false;
    if (instr_reads_auto_slot(i, i->addr) || instr_writes_auto_slot(i, i->addr)) {
        *off = i->addr;
        return true;
    }
    return false;
}

//
// Compute, for each auto slot, whether it is referenced in more than one basic block.
// A temporary confined to a single block is never live across an edge, which licenses
// the block-end case of rule #28.  Returns a freshly allocated array of size `num_autos`
// (NULL when num_autos == 0); the caller frees it.
//
static bool *compute_multiblock(const Besm_Block *block, int num_autos)
{
    if (num_autos <= 0)
        return NULL;
    bool *multiblock = (bool *)xalloc(num_autos * sizeof(bool), __func__, __FILE__, __LINE__);
    int *firstblk    = (int *)xalloc(num_autos * sizeof(int), __func__, __FILE__, __LINE__);
    for (int i = 0; i < num_autos; i++) {
        multiblock[i] = false;
        firstblk[i]   = -1;
    }

    int blockidx = 0;
    for (const Besm_Instr *i = block->body; i; i = i->next) {
        int off;
        if (instr_auto_slot_ref(i, &off) && off >= 0 && off < num_autos) {
            if (firstblk[off] < 0)
                firstblk[off] = blockidx;
            else if (firstblk[off] != blockidx)
                multiblock[off] = true;
        }
        if (is_block_boundary(i))
            blockidx++;
    }

    xfree(firstblk);
    return multiblock;
}

// Splice a single node out of a block's list and free it.  `besm_free_instr`
// recurses on ->next, so unlink first (set cur->next = NULL) to free only `cur`.
static void delete_instr(Besm_Block *block, Besm_Instr *prev, Besm_Instr *cur)
{
    if (prev)
        prev->next = cur->next;
    else
        block->body = cur->next;
    cur->next = NULL;
    besm_free_instr(cur);
}

// Splice a whole C group — `count` consecutive nodes starting at `first` — out of a block's
// list and free them.  Relink around the run before freeing anything, then free node by
// node (`besm_free_instr` recurses on ->next).
static void delete_group(Besm_Block *block, Besm_Instr *prev, Besm_Instr *first, int count)
{
    Besm_Instr *after = first;
    for (int n = 0; n < count; n++)
        after = after->next;
    if (prev)
        prev->next = after;
    else
        block->body = after;

    Besm_Instr *cur = first;
    for (int n = 0; n < count; n++) {
        Besm_Instr *next = cur->next;
        cur->next        = NULL;
        besm_free_instr(cur);
        cur = next;
    }
}

// Splice `count` nodes out at the cursor and return the new cursor (the node that follows
// the run).  `prev` and the tracked state stay valid across it — nothing deleted this way
// changes A, R or ω.
static Besm_Instr *delete_run(Besm_Block *block, Besm_Instr *prev, Besm_Instr *cur, int count)
{
    Besm_Instr *after = cur;
    for (int n = 0; n < count; n++)
        after = after->next;
    delete_group(block, prev, cur, count);
    return after;
}

// One forward sweep over a block.  Returns true if any node was deleted.
static bool peephole_sweep(Besm_Block *block, const Frame *frame, const bool *multiblock)
{
    PeepState st;
    state_reset(&st);

    bool changed       = false;
    Besm_Instr *prev   = NULL;
    Besm_Instr *cur    = block->body;

    while (cur) {
        // Rule #31(b): unreachable tail.  Code after an unconditional transfer and
        // before the next label/directive can never execute; delete it.
        if (st.in_unreachable && unreachable_deletable(cur)) {
            Besm_Instr *next = cur->next;
            delete_instr(block, prev, cur);
            cur     = next;
            changed = true;
            continue; // still unreachable: re-test the new cur (prev/st unchanged)
        }

        // A C group is stepped and rewritten as one unit, so the cursor skips from its
        // setter straight past its consumer.  That is what makes it structurally impossible
        // for any rule below to delete a consumer and leave the setter behind to re-bind C
        // to whatever fell into the gap.  (The unreachable-tail rule above is the one
        // exception, and it is safe: it deletes a run head-first, so a group's setter goes
        // before its consumer is ever the cursor.)
        if (is_c_setter(cur)) {
            // Rule #32(b) for a global address: the anchor is the whole `utc g` + `xta`
            // group, so it has to be caught before the group is stepped past.
            int fold = try_io_memory_address(cur);
            if (fold > 0) {
                cur     = delete_run(block, prev, cur, fold);
                changed = true;
                continue;
            }

            int count;
            Besm_Instr *consumer = c_group_consumer(cur, &count);
            if (consumer != NULL) {
                Loc gl = c_group_loc(cur);

                // Rule #27 for a global: the whole `utc name` + `xta` group reloads a
                // location A already holds.  Delete setter and consumer together.
                if (consumer->kind == BESM_MEM_XTA && !consumer->is_volatile &&
                    loc_eq(gl, st.a_loc)) {
                    Besm_Instr *next = consumer->next;
                    delete_group(block, prev, cur, count);
                    cur     = next;
                    changed = true;
                    continue; // prev and tracked state stay valid
                }

                // Step the group.  A word access settles A on the location it touched (a
                // store leaves A mirroring what it wrote); a dereference or an address
                // computation names no location, and every other consumer — `xts`, `asx`,
                // arithmetic, `vtm`, `vjm` — clobbers A.  Both land on LOC_NONE.  No group
                // member is a SETR, so R is unchanged.
                if (consumer->kind == BESM_MEM_XTA || consumer->kind == BESM_MEM_ATX)
                    st.a_loc = gl;
                else
                    st.a_loc = loc_none();
                if (is_block_boundary(consumer)) // `wtc` + `vjm`: the indirect call
                    state_reset(&st);
                // Only the consumer can touch ω: UTC and WTC keep it.  A group whose
                // consumer is a branch is not a shape instruction selection emits, so
                // rule #33 never has to look inside one.
                omega_step(&st, consumer);

                prev = consumer;
                cur  = consumer->next;
                continue;
            }
            // Malformed: the block ends on a setter.  Fall through to the single-node path.
        }

        bool deleted = false;
        for (size_t r = 0; r < NUM_RULES; r++) {
            if (rule_table[r](cur, &st)) {
                Besm_Instr *next = cur->next;
                delete_instr(block, prev, cur);
                cur     = next;
                changed = true;
                deleted = true;
                break;
            }
        }
        // Rule #28: dead temp-store elimination (needs look-ahead + the frame).
        // Rule #29(b): dead NTR elimination (needs forward look-ahead).
        // Rule #31(a): jump to the immediately following label (needs look-ahead).
        if (!deleted && (dead_temp_store(cur, frame, multiblock) || dead_ntr_set(cur) ||
                         jump_to_next_label(cur))) {
            Besm_Instr *next = cur->next;
            delete_instr(block, prev, cur);
            cur     = next;
            changed = true;
            deleted = true;
        }
        // Rule #32: I/O address folding.  Each removes one or two nodes at the cursor and
        // rewrites the `xts`/`wtc`/`ext` trailer that follows in place.
        if (!deleted) {
            int fold = try_io_displacement_fusion(cur);
            if (fold == 0)
                fold = try_io_memory_address(cur);
            if (fold > 0) {
                cur     = delete_run(block, prev, cur, fold);
                changed = true;
                deleted = true;
            }
        }
        // Rule #31(c): invert a conditional that only skips an unconditional jump.
        // It mutates `cur` in place and deletes the following `uj`, so re-test `cur`.
        if (!deleted && try_invert_branch_over_jump(block, cur)) {
            changed = true;
            continue; // prev and tracked state stay valid; re-test the rewritten cur
        }
        if (deleted)
            continue; // prev and tracked state stay valid; re-test the new cur

        // Rule #33: ω fixup.  The one rewrite that *inserts* a node — a bare `aex` ahead of
        // a conditional branch the tracked state cannot vouch for.  Re-testing `cur` would
        // loop forever, so record the new group and fall through to the boundary step; the
        // next sweep sees logical ω here and inserts nothing.
        if (needs_omega_fixup(cur, &st)) {
            Besm_Instr *fix = besm_new_instr(BESM_LOG_AEX);
            insert_before(block, prev, cur, fix);
            prev     = fix;
            st.omega = OMEGA_LOGICAL;
            changed  = true;
        }

        if (is_block_boundary(cur)) {
            state_reset(&st);
            // A CALL returns with logical ω, and so do the extracode and a read-address
            // `ext`/`mod`; `omega_after` knows which, and rule #33 relies on it to leave
            // the compare → branch fusion (#30) alone.
            omega_step(&st, cur);
            // `b/save`/`b/save0` leave R = 7; seed it so a redundant `ntr 7` just after
            // the prologue (or anywhere R is already 7) is recognised.  Every other CALL
            // may change R (the arithmetic helpers borrow the FP unit), so R stays
            // unknown there.
            if (cur->kind == BESM_BRANCH_CALL && cur->name != NULL &&
                (strcmp(cur->name, "b$save") == 0 || strcmp(cur->name, "b$save0") == 0)) {
                st.r_known = true;
                st.r_val   = 7;
            }
            // An unconditional transfer (uj) makes the following instructions unreachable
            // until the next label or structural directive (rule #31(b)).  A `stop` is NOT
            // one: the halt is resumable — the operator presses continue and execution goes
            // on at the next instruction — so what follows it is live code.
            if (cur->kind == BESM_BRANCH_UJ)
                st.in_unreachable = true;
        } else {
            state_step(&st, cur);
        }

        prev = cur;
        cur  = cur->next;
    }
    return changed;
}

void besm_peephole(Besm_Func *func, const Frame *frame)
{
    if (!func)
        return;
    int num_autos = frame ? frame_num_autos(frame) : 0;
    for (Besm_Func *fn = func; fn; fn = fn->next) {
        for (Besm_Block *block = fn->blocks; block; block = block->next) {
            // The multi-block classification only ever goes stale in the safe direction.
            // Deleting a `wtc %p` + `xta` reload group drops a read of `%p`, which may
            // have been that block's only reference to the slot; the slot then stays
            // marked multi-block when it is no longer, and rule #28 keeps a store it could
            // have dropped.  Nothing can add a reference, so it is never marked
            // single-block wrongly.  Compute it once before the fixpoint loop.
            bool *multiblock = compute_multiblock(block, num_autos);
            // Iterate to a fixpoint: one rewrite can expose another.
            while (peephole_sweep(block, frame, multiblock))
                ;
            if (multiblock)
                xfree(multiblock);
        }
    }
}
