#include "frame.h"

#include <stdint.h>

#include "abi.h"
#include "string_map.h"
#include "xalloc.h"

// Encode (reg, offset, temp) into a single intptr_t so we can store it in the map.
// reg occupies bits 16-19; a "compiler temporary" marker is packed at bit 20 so the
// peephole pass can recover temp-ness from an auto slot. (The marker is now also
// derivable from the key via name_is_temp(); the packing is kept for compactness.)
#define SLOT_ENCODE(reg, off, temp) \
    (((intptr_t)((temp) ? 1 : 0) << 20) | ((intptr_t)(reg) << 16) | (intptr_t)(off))
#define SLOT_REG(v)  ((((int)(v)) >> 16) & 0xf)
#define SLOT_OFF(v)  (((int)(v)) & 0xffff)
#define SLOT_TEMP(v) ((((int)(v)) >> 20) & 1)

struct Frame {
    StringMap slots; // name -> SLOT_ENCODE(reg, offset, temp)
    int num_autos;
    bool *auto_is_temp; // size num_autos; true if that auto slot holds a '%'+digit temporary
};

// A TAC name denotes a compiler temporary (new_temp) when it is '%' followed by a digit;
// parameters and named locals are '%' followed by a letter or '_'.
static bool name_is_temp(const char *name)
{
    return name[0] == '%' && name[1] >= '0' && name[1] <= '9';
}

//
// Register a frame-resident variable name in the frame unless it is already
// present. Frame-resident names start with '%'; any other name is a parameter
// (seeded directly in frame_build) or a module-level global and is skipped.
//
static void assign_if_new(Frame *f, const char *name, int reg, int *counter)
{
    if (name[0] != '%')
        return; // parameter or module-level global — not an auto slot
    intptr_t dummy;
    if (map_get(&f->slots, name, &dummy))
        return; // already assigned (e.g. a parameter)
    map_insert(&f->slots, name, SLOT_ENCODE(reg, *counter, name_is_temp(name)), 0);
    (*counter)++;
}

//
// Visit all Tac_Val * values in a chain and register VAR names as auto slots.
//
static void collect_vals(Frame *f, const Tac_Val *v, int *auto_count)
{
    for (; v; v = v->next) {
        if (v->kind == TAC_VAL_VAR)
            assign_if_new(f, v->u.var_name, REG_AUTO, auto_count);
    }
}

//
// Visit all values referenced by a single instruction.
//
static void collect_instr(Frame *f, const Tac_Instruction *instr, int *auto_count)
{
    switch (instr->kind) {
    case TAC_INSTRUCTION_RETURN:
        collect_vals(f, instr->u.return_.src, auto_count);
        break;
    // All type-conversion instructions follow the {src, dst} pattern.
    case TAC_INSTRUCTION_SIGN_EXTEND:
        collect_vals(f, instr->u.sign_extend.src, auto_count);
        collect_vals(f, instr->u.sign_extend.dst, auto_count);
        break;
    case TAC_INSTRUCTION_TRUNCATE:
        collect_vals(f, instr->u.truncate.src, auto_count);
        collect_vals(f, instr->u.truncate.dst, auto_count);
        break;
    case TAC_INSTRUCTION_ZERO_EXTEND:
        collect_vals(f, instr->u.zero_extend.src, auto_count);
        collect_vals(f, instr->u.zero_extend.dst, auto_count);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_INT:
        collect_vals(f, instr->u.double_to_int.src, auto_count);
        collect_vals(f, instr->u.double_to_int.dst, auto_count);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_UINT:
        collect_vals(f, instr->u.double_to_uint.src, auto_count);
        collect_vals(f, instr->u.double_to_uint.dst, auto_count);
        break;
    case TAC_INSTRUCTION_INT_TO_DOUBLE:
        collect_vals(f, instr->u.int_to_double.src, auto_count);
        collect_vals(f, instr->u.int_to_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_UINT_TO_DOUBLE:
        collect_vals(f, instr->u.uint_to_double.src, auto_count);
        collect_vals(f, instr->u.uint_to_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_FLOAT_TO_DOUBLE:
        collect_vals(f, instr->u.float_to_double.src, auto_count);
        collect_vals(f, instr->u.float_to_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_FLOAT:
        collect_vals(f, instr->u.double_to_float.src, auto_count);
        collect_vals(f, instr->u.double_to_float.dst, auto_count);
        break;
    case TAC_INSTRUCTION_INT_TO_FLOAT:
        collect_vals(f, instr->u.int_to_float.src, auto_count);
        collect_vals(f, instr->u.int_to_float.dst, auto_count);
        break;
    case TAC_INSTRUCTION_UINT_TO_FLOAT:
        collect_vals(f, instr->u.uint_to_float.src, auto_count);
        collect_vals(f, instr->u.uint_to_float.dst, auto_count);
        break;
    case TAC_INSTRUCTION_FLOAT_TO_INT:
        collect_vals(f, instr->u.float_to_int.src, auto_count);
        collect_vals(f, instr->u.float_to_int.dst, auto_count);
        break;
    case TAC_INSTRUCTION_FLOAT_TO_UINT:
        collect_vals(f, instr->u.float_to_uint.src, auto_count);
        collect_vals(f, instr->u.float_to_uint.dst, auto_count);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_INT:
        collect_vals(f, instr->u.long_double_to_int.src, auto_count);
        collect_vals(f, instr->u.long_double_to_int.dst, auto_count);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_UINT:
        collect_vals(f, instr->u.long_double_to_uint.src, auto_count);
        collect_vals(f, instr->u.long_double_to_uint.dst, auto_count);
        break;
    case TAC_INSTRUCTION_INT_TO_LONG_DOUBLE:
        collect_vals(f, instr->u.int_to_long_double.src, auto_count);
        collect_vals(f, instr->u.int_to_long_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_UINT_TO_LONG_DOUBLE:
        collect_vals(f, instr->u.uint_to_long_double.src, auto_count);
        collect_vals(f, instr->u.uint_to_long_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_DOUBLE:
        collect_vals(f, instr->u.long_double_to_double.src, auto_count);
        collect_vals(f, instr->u.long_double_to_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_DOUBLE_TO_LONG_DOUBLE:
        collect_vals(f, instr->u.double_to_long_double.src, auto_count);
        collect_vals(f, instr->u.double_to_long_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_LONG_DOUBLE_TO_FLOAT:
        collect_vals(f, instr->u.long_double_to_float.src, auto_count);
        collect_vals(f, instr->u.long_double_to_float.dst, auto_count);
        break;
    case TAC_INSTRUCTION_FLOAT_TO_LONG_DOUBLE:
        collect_vals(f, instr->u.float_to_long_double.src, auto_count);
        collect_vals(f, instr->u.float_to_long_double.dst, auto_count);
        break;
    case TAC_INSTRUCTION_PTR_TO_CHAR_PTR:
        collect_vals(f, instr->u.ptr_to_char_ptr.src, auto_count);
        collect_vals(f, instr->u.ptr_to_char_ptr.dst, auto_count);
        break;
    case TAC_INSTRUCTION_CHAR_PTR_TO_PTR:
        collect_vals(f, instr->u.char_ptr_to_ptr.src, auto_count);
        collect_vals(f, instr->u.char_ptr_to_ptr.dst, auto_count);
        break;
    case TAC_INSTRUCTION_UNARY:
        collect_vals(f, instr->u.unary.src, auto_count);
        collect_vals(f, instr->u.unary.dst, auto_count);
        break;
    case TAC_INSTRUCTION_BINARY:
        collect_vals(f, instr->u.binary.src1, auto_count);
        collect_vals(f, instr->u.binary.src2, auto_count);
        collect_vals(f, instr->u.binary.dst, auto_count);
        break;
    case TAC_INSTRUCTION_COPY:
        collect_vals(f, instr->u.copy.src, auto_count);
        collect_vals(f, instr->u.copy.dst, auto_count);
        break;
    case TAC_INSTRUCTION_GET_ADDRESS:
    case TAC_INSTRUCTION_GET_ADDRESS_BYTE:
    case TAC_INSTRUCTION_GET_ADDRESS_DECAY:
        // src is a local variable (gets a slot) or a global (skipped by assign_if_new).
        if (instr->u.get_address.src->kind == TAC_VAL_VAR)
            assign_if_new(f, instr->u.get_address.src->u.var_name, REG_AUTO, auto_count);
        collect_vals(f, instr->u.get_address.dst, auto_count);
        break;
    case TAC_INSTRUCTION_LOAD:
    case TAC_INSTRUCTION_LOAD_BYTE:
        collect_vals(f, instr->u.load.src_ptr, auto_count);
        collect_vals(f, instr->u.load.dst, auto_count);
        break;
    case TAC_INSTRUCTION_STORE:
    case TAC_INSTRUCTION_STORE_BYTE:
        collect_vals(f, instr->u.store.src, auto_count);
        collect_vals(f, instr->u.store.dst_ptr, auto_count);
        break;
    case TAC_INSTRUCTION_ADD_PTR:
        collect_vals(f, instr->u.add_ptr.ptr, auto_count);
        collect_vals(f, instr->u.add_ptr.index, auto_count);
        collect_vals(f, instr->u.add_ptr.dst, auto_count);
        break;
    case TAC_INSTRUCTION_PTR_DIFF:
        collect_vals(f, instr->u.ptr_diff.ptr_a, auto_count);
        collect_vals(f, instr->u.ptr_diff.ptr_b, auto_count);
        collect_vals(f, instr->u.ptr_diff.dst, auto_count);
        break;
    case TAC_INSTRUCTION_COPY_TO_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_TO_OFFSET:
        collect_vals(f, instr->u.copy_to_offset.src, auto_count);
        assign_if_new(f, instr->u.copy_to_offset.dst, REG_AUTO, auto_count);
        break;
    case TAC_INSTRUCTION_COPY_FROM_OFFSET:
    case TAC_INSTRUCTION_COPY_BYTE_FROM_OFFSET:
        assign_if_new(f, instr->u.copy_from_offset.src, REG_AUTO, auto_count);
        collect_vals(f, instr->u.copy_from_offset.dst, auto_count);
        break;
    case TAC_INSTRUCTION_JUMP:
    case TAC_INSTRUCTION_LABEL:
        break; // no values
    case TAC_INSTRUCTION_JUMP_IF_ZERO:
        collect_vals(f, instr->u.jump_if_zero.condition, auto_count);
        break;
    case TAC_INSTRUCTION_JUMP_IF_NOT_ZERO:
        collect_vals(f, instr->u.jump_if_not_zero.condition, auto_count);
        break;
    case TAC_INSTRUCTION_JUMP_TABLE: // only coroutines make one, and BESM-6 has none
        collect_vals(f, instr->u.jump_table.index, auto_count);
        break;
    case TAC_INSTRUCTION_FUN_CALL:
    case TAC_INSTRUCTION_FUN_CALL_NORETURN:
        collect_vals(f, instr->u.fun_call.args, auto_count);
        collect_vals(f, instr->u.fun_call.dst, auto_count);
        break;
    case TAC_INSTRUCTION_ALLOCATE_LOCAL:
        break; // multi-word slot reserved in a dedicated first pass (frame_build)
    }
}

//
// Reserve a contiguous multi-word frame slot for an aggregate local described by
// an AllocateLocal pseudo-instruction. Run before the scalar scan so the aggregate
// occupies one unbroken block of words; scalars then fill the remaining slots.
//
static void allocate_aggregate(Frame *f, const Tac_Instruction *instr, int *auto_count)
{
    const char *name = instr->u.allocate_local.name;
    if (name[0] != '%')
        return; // defensive: aggregate locals are always '%'-prefixed
    intptr_t dummy;
    if (map_get(&f->slots, name, &dummy))
        return; // already assigned
    // AllocateLocal carries target bytes; convert to whole words (round up).
    int size_words  = (instr->u.allocate_local.size + BESM6_WORD_BYTES - 1) / BESM6_WORD_BYTES;
    int align_words = (instr->u.allocate_local.alignment + BESM6_WORD_BYTES - 1) / BESM6_WORD_BYTES;
    if (align_words < 1)
        align_words = 1;
    if (align_words > 1 && (*auto_count % align_words) != 0)
        *auto_count += align_words - (*auto_count % align_words);
    // An aggregate slot is never a temporary, even under a temporary's name (an sret,
    // conditional or compound-literal slot): it spans several words and its address
    // may be taken, so its stores are not dead and it is never reclaimed.
    map_insert(&f->slots, name, SLOT_ENCODE(REG_AUTO, *auto_count, false), 0);
    *auto_count += size_words;
}

// map_iterate callback: record temp-ness of each auto slot into the bool array.
typedef struct {
    bool *arr;
    int num;
} TempFill;

static void fill_auto_is_temp(const char *key, intptr_t value, const void *arg)
{
    (void)key;
    const TempFill *tf = (const TempFill *)arg;
    if (SLOT_REG(value) != REG_AUTO)
        return; // params (REG_PAR) are never temporaries
    int off = SLOT_OFF(value);
    if (off >= 0 && off < tf->num)
        tf->arr[off] = SLOT_TEMP(value);
}

Frame *frame_build(const Tac_TopLevel *fn, const Tac_TopLevel *program)
{
    (void)program; // local/global is now encoded in the name (leading '%')

    Frame *f        = (Frame *)xalloc(sizeof(Frame), __func__, __FILE__, __LINE__);
    f->num_autos    = 0;
    f->auto_is_temp = NULL;
    map_init(&f->slots);

    // Assign params first (REG_PAR, 0..N-1). Param names are '%'-prefixed too, but a
    // parameter is never a compiler temporary.
    int par_count = 0;
    for (const Tac_Param *p = fn->u.function.params; p; p = p->next) {
        map_insert(&f->slots, p->name, SLOT_ENCODE(REG_PAR, par_count, false), 0);
        par_count++;
    }

    // First pass: reserve contiguous slots for aggregate locals (arrays, structs)
    // so their words do not overlap adjacent scalar slots.
    for (const Tac_Instruction *instr = fn->u.function.body; instr; instr = instr->next)
        if (instr->kind == TAC_INSTRUCTION_ALLOCATE_LOCAL)
            allocate_aggregate(f, instr, &f->num_autos);

    // Second pass: scan the body for '%'-prefixed names; assign one-word auto slots
    // (REG_AUTO) to those not already assigned (params or aggregates). Non-prefixed
    // names are module-level globals (skipped).
    for (const Tac_Instruction *instr = fn->u.function.body; instr; instr = instr->next)
        collect_instr(f, instr, &f->num_autos);

    // Build the reverse auto-slot -> temp? lookup the peephole pass consults.
    if (f->num_autos > 0) {
        f->auto_is_temp = (bool *)xalloc(f->num_autos * sizeof(bool), __func__, __FILE__, __LINE__);
        for (int i = 0; i < f->num_autos; i++)
            f->auto_is_temp[i] = false;
        TempFill tf = { f->auto_is_temp, f->num_autos };
        map_iterate(&f->slots, fill_auto_is_temp, &tf);
        // A temporary whose address is taken is read through it (the result of a call
        // returning a one-word struct, whose member is then loaded): not one only ever
        // read by name, which is what the peephole's dead-store rule relies on.
        for (const Tac_Instruction *instr = fn->u.function.body; instr; instr = instr->next) {
            if (instr->kind != TAC_INSTRUCTION_GET_ADDRESS &&
                instr->kind != TAC_INSTRUCTION_GET_ADDRESS_BYTE &&
                instr->kind != TAC_INSTRUCTION_GET_ADDRESS_DECAY)
                continue;
            const Tac_Val *src = instr->u.get_address.src;
            int reg, off;
            if (src && src->kind == TAC_VAL_VAR && frame_lookup(f, src->u.var_name, &reg, &off) &&
                reg == REG_AUTO && off >= 0 && off < f->num_autos)
                f->auto_is_temp[off] = false;
        }
    }

    return f;
}

bool frame_slot_is_temp(const Frame *f, int reg, int off)
{
    return reg == REG_AUTO && off >= 0 && off < f->num_autos && f->auto_is_temp[off];
}

bool frame_lookup(const Frame *f, const char *name, int *reg, int *offset)
{
    intptr_t v;
    if (!map_get(&f->slots, name, &v))
        return false;
    *reg    = SLOT_REG(v);
    *offset = SLOT_OFF(v);
    return true;
}

int frame_num_autos(const Frame *f)
{
    return f->num_autos;
}

void frame_free(Frame *f)
{
    map_destroy(&f->slots);
    if (f->auto_is_temp)
        xfree(f->auto_is_temp);
    xfree(f);
}
