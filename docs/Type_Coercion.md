# Type Coercion in C11

This document describes the implicit type conversion rules defined by the C11
standard and explains how this compiler implements them.  It is intended as a
companion to `semantic/coercion_tests.cpp`.

---

## 1. Where coercion fires

Implicit conversions happen in exactly four contexts:

| Context | Standard reference | Compiler site |
|---|---|---|
| Assignment `lhs = rhs` | §6.5.16.1 | `coerce_for_assignment()` called from `initializers.c` and `expressions.c` (EXPR_ASSIGN) |
| Compound assignment `lhs op= rhs` | §6.5.16.2, §6.5.16.3 | EXPR_ASSIGN dispatch in `expressions.c` (non-SIMPLE ops) |
| Function call argument | §6.5.2.2 p7 | `coerce_for_assignment()` called for each argument in `expressions.c` (EXPR_CALL) |
| Return statement | §6.8.6.4 p3 | `coerce_for_assignment()` called in `statements.c` (STMT_RETURN) |

Simple assignment, function arguments, and return statements apply the **assignment
conversion rules** described in §4 below.  Compound assignment operators use a separate
dispatch described in §4a.
Binary and unary expressions additionally apply **integer promotions** and the
**usual arithmetic conversions**.

---

## 2. Integer promotions (§6.3.1.1)

Before most arithmetic and bitwise operations every operand narrower than `int`
is widened:

- If `int` can represent all values of the original type → promote to **`int`**
- Otherwise → promote to **`unsigned int`**

**Types affected**: `char`, `signed char`, `unsigned char`, `short`,
`unsigned short`, and single-bit `_Bool`.

**When they apply**: unary `+`, `-`, `~`; both operands of binary `<<`, `>>`,
`&`, `|`, `^`; and as the first step of the usual arithmetic conversions.

The compiler inserts an `EXPR_CAST` node for each promoted operand.  The cast
target kind is always `TYPE_INT` (the compiler uses `int`-width integers as the
minimum result type; `unsigned int` promotion of very-wide `unsigned char` is
not currently distinguished from the signed case).

---

## 3. Usual arithmetic conversions (§6.3.1.8)

Applied to both operands of `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `==`,
`!=`, `<`, `>`, `<=`, `>=` to find a **common type**:

1. Apply integer promotions to both operands.
2. If both operands now have the same type → done; that is the result type.
3. If either operand is `double` → convert the other to `double`.
4. If either operand is `float` → convert the other to `float`.
5. If both are integers of the same size:
   - if one is unsigned and the other signed → result is unsigned.
6. Otherwise → the operand with the larger size wins.

The result is computed by `get_common_type()` in `semantic/typecheck.c`, which gives
`long double` the highest floating rank (a mixed `long double` operand wins).  On RISC-V
the three are distinct formats — `float` is IEEE binary32, `double` binary64 and
`long double` binary128 — so each widening or narrowing is a real conversion (a
binary128 one goes through the software runtime in `libc/riscv/float128.c`). A target
whose formats coincide (BESM-6 has one native FP word for all three) gets a plain copy
instead.

---

## 4. Assignment constraints (§6.5.16.1)

The left-hand side must be a modifiable lvalue.  The conversion rules for the
right-hand side are:

| RHS type | LHS type | Allowed? | Conversion |
|---|---|---|---|
| Arithmetic | Arithmetic | **Yes** | Implicit widening or narrowing cast |
| Any object pointer | `void *` | **Yes** | Implicit cast |
| `void *` | Any object pointer | **Yes** | Implicit cast |
| Integer constant `0` (null pointer constant) | Any pointer | **Yes** | Implicit cast to null pointer |
| Same struct/union type | Same struct/union type | **Yes** | No cast (direct copy) |
| Compatible function pointer (same return type; either side old-style) | Function pointer | **Yes** | No cast |
| Incompatible pointer | Any pointer | **No** | `fatal_error` |
| Integer (non-zero) | Any pointer | **No** | `fatal_error` |
| Any pointer | Integer | **No** | `fatal_error` |
| Different struct/union type | Any struct/union type | **No** | `fatal_error` |

### Conversion to `_Bool` (§6.3.1.2)

Converting *to* `_Bool` is the one asymmetric arithmetic conversion: the result is
**0 if the value compares equal to 0 and 1 otherwise** — a zero test, not a truncation.
It applies to any scalar source, including a pointer and a floating value, so
`(_Bool)0.5` is 1 and `(_Bool)256` is 1 rather than 0.

Every runtime conversion to `_Bool` is lowered by `emit_bool_normalize()`
(`translator/translate.c`), which emits `dst = (src != 0)`.  `emit_cast()` calls it
before any width logic, which covers assignment, initialization of an automatic object,
argument passing, return and the explicit cast — all of which the typechecker turns into
one `EXPR_CAST` node.  Two paths do not build a cast and normalize on their own:
`++`/`--` (in `gen_step`, so `b++` on a true `_Bool` stays 1), and a static initializer
(folded to 0/1 in `semantic/const_convert.c`).  Reading a `_Bool` *out* needs nothing —
it already holds 0 or 1.

### Null pointer constant

A **null pointer constant** is an integer constant expression with value `0`, or
`(void *)0`.  Either form may be implicitly converted to any pointer type,
yielding a null pointer.  The compiler recognises the integer literal form in
`is_null_pointer_constant()`.  The `(void *)0` cast form is handled
automatically by the void-pointer compatibility rule above.

### void* compatibility

`void *` converts implicitly to and from any object pointer.  This rule does
**not** apply to function pointers.

### struct/union compatibility

When both sides have kind `TYPE_STRUCT` (or `TYPE_UNION`),
`compatible_type()` compares the tag names with `strcmp`.
Assigning `struct B` to `struct A` is therefore correctly rejected even though
both share the same `TypeKind`.

### Function pointer compatibility

Two function-pointer types are **compatible** when `compatible_type()` returns
true for their pointee function types.  Two function types are compatible when:

- Their `variadic` flags match.
- Their return types are recursively compatible.
- **Either** side has no parameter list — an old-style declaration `int f()`
  or a `(void)` prototype both count as "no params" and are accepted against
  any parameter list with a matching return type.
- **Or** both have parameter lists and every parameter type is pairwise
  compatible (`compare_param`).

This allows an old-style function such as `int foo();` to initialize or be
assigned to a `int (*)(int *)` function pointer:

```c
int foo();              // old-style: no prototype
int (*bar)(int *) = foo; // OK — foo's no-param list matches anything
```

The `params_for_compat()` helper in `semantic/typecheck.c` normalises the
`(void)` sentinel (a single unnamed `TYPE_VOID` param) to NULL so it is
treated identically to an empty parameter list.

---

## 4a. Compound assignment operators (§6.5.16.2, §6.5.16.3)

The compound assignment operators are: `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `|=`.
Unlike simple assignment they do **not** go through `coerce_for_assignment()`; instead the
EXPR_ASSIGN handler in `expressions.c` dispatches directly on the operator and lhs type:

| Operator | LHS type | Sub-case |
|---|---|---|
| `+=`, `-=` | Complete pointer | Additive pointer arithmetic (§6.5.6 p2) |
| `+=`, `-=` | Arithmetic | Arithmetic compound |
| `*=`, `/=`, `%=` | Arithmetic | Arithmetic compound |
| `<<=`, `>>=` | Arithmetic | Arithmetic compound |
| `&=`, `^=`, `\|=` | Arithmetic | Arithmetic compound |

### Sub-case 1 — Additive pointer arithmetic (`+=`, `-=` on a pointer lhs)

- RHS must be an integer type.  A non-integer RHS (floating point, pointer) is a
  `fatal_error("Pointer arithmetic requires integer operand")`.
- RHS is converted to `long` via `convert_to_kind(rhs, TYPE_LONG)` (§6.5.6 p2 — the
  offset type for pointer arithmetic is `ptrdiff_t`, represented here as `long`).
- If the RHS is already `long` no cast node is inserted.
- Result type: the lhs pointer type.

### Sub-case 2 — Arithmetic compound assignment (all other compound ops)

- Both lhs and rhs must be arithmetic types.  A pointer on either side is a
  `fatal_error("Invalid operands for compound assignment")`.
- RHS is converted to the **lhs type** via `convert_to_type(rhs, lhs->type)`.
  This permits silent narrowing (e.g. `int x; double y; x += y;` inserts a
  `double → int` cast) — no diagnostic is issued, matching the C standard.
- If the RHS already has the same kind as the lhs no cast node is inserted.
- Result type: the lhs type.
- **Narrow lvalue**: when the lhs is narrower than `int` (`is_promotable_narrow`) and the
  operator is arithmetic, the rhs is converted to `get_common_type(lhs, rhs)` instead, so
  the operation runs in the promoted type and its result is converted back to the lhs
  type — as `lhs = lhs op rhs` would.  A `_Bool` lvalue takes that path for **every**
  compound operator, bitwise and shift included: the conversion back is where `b <<= 4`
  and `b |= 4` re-normalize to 0/1 (§6.3.1.2 above).

**Key difference from simple assignment (§4)**: compound operators narrow the rhs directly
to the lhs type without going through `coerce_for_assignment()`.  Pointer↔pointer and
pointer↔integer combinations that `coerce_for_assignment()` would reject with
"Cannot convert type for assignment" instead reach the arithmetic check above and are
rejected with "Invalid operands for compound assignment".

---

## 5. Function argument coercion (§6.5.2.2)

With a visible prototype, each argument undergoes implicit conversion as if by
assignment to the corresponding parameter type.  The same rules as §4 above
apply.  Variadic arguments (`...`) additionally undergo the **default argument
promotions**: `float` → `double`, and integer promotions; the compiler does not
implement variadic promotion separately because calls to variadic functions are
currently typed via the fixed-parameter portion of the prototype.

### Parameter type adjustment (§6.7.6.3 p7)

Before the assignment conversion rules are applied, each parameter type is
**adjusted**:

- A parameter declared as `T array[N]` or `T array[]` is treated as `T *`.
- A parameter declared as a function type is treated as a pointer to function.

In practice this means passing an array variable to an array parameter is
valid: the argument decays to `T *` (§6.3.2.1 p3) and the adjusted parameter
type is also `T *`, so no cast is inserted.

```c
int foo[42];
void bar(int a[42]);   // parameter adjusted to int *
bar(foo);              // foo decays to int *; types match, no cast
```

The compiler applies this adjustment inside `coerce_for_assignment()` by
accepting a `TYPE_POINTER` argument against a `TYPE_ARRAY` target when the
pointer's element kind matches the array's element kind.

---

## 6. Return statement (§6.8.6.4)

The return value is converted as if by assignment to the function's declared
return type.  The same rules as §4 above apply.  Returning any value from a
`void` function, or returning no value from a non-`void` function, produces a
`fatal_error`.

---

## 7. Compiler implementation summary

### `coerce_for_assignment(e, target_type)` — `semantic/typecheck.c:256`

```
if kinds match AND compatible_type(target_type, e_type)
    → return e unchanged  (no cast)
if both arithmetic
    → convert_to_type(e, target_type)  (insert EXPR_CAST)
if e is a null pointer constant AND target is pointer
    → convert_to_type(e, target_type)
if (target is void* AND e is any pointer)
   OR (target is any pointer AND e is void*)
    → convert_to_type(e, target_type)
if e_type is pointer AND target_type is array
   AND pointer-element kind == array-element kind
    → return e unchanged  (C11 §6.7.6.3p7 array parameter adjustment)
otherwise
    → fatal_error("Cannot convert type for assignment")
```

### EXPR_ASSIGN compound dispatch — `semantic/expressions.c:372`

```
typecheck and decay both lhs and rhs
if op is '='
    → coerce_for_assignment(rhs, lhs type)
if op is '+=' or '-=' AND lhs is a complete pointer type
    if rhs is not an integer type
        → fatal_error("Pointer arithmetic requires integer operand")
    → convert_to_kind(rhs, long)
otherwise (any other compound op, or '+=' or '-=' with arithmetic lhs)
    if lhs or rhs is not arithmetic
        → fatal_error("Invalid operands for compound assignment")
    → convert_to_type(rhs, lhs type)
result type ← lhs type
```

### `compatible_type(target, src)` — `semantic/typecheck.c`

Used by `coerce_for_assignment()` for the same-kind check and by
`build_static_init()` for pointer initializers.

```
if either is NULL → return (target == src)
if kinds differ → return false
TYPE_FUNCTION:
    if variadic flags differ → return false
    if return types not compatible → return false
    tp ← params_for_compat(target)  // NULL if old-style or f(void)
    sp ← params_for_compat(src)
    if tp is NULL OR sp is NULL → return true  (old-style: skip param check)
    → compare_param(tp, sp)
TYPE_POINTER:
    → compatible_type(target->target, src->target)
TYPE_STRUCT / TYPE_UNION:
    → strcmp(tag names) == 0
default:
    → compare_type(target, src)
```

### `get_common_type(t1, t2)` — `semantic/typecheck.c:184`

```
promote char operands to int
promote short/unsigned short operands to int
if same kind → return t1
if either is double → return double
if either is float  → return float
if same size → return unsigned one
return the larger type
```

### `convert_to_type(e, target_type)` — `semantic/typecheck.c:151`

Wraps `e` in an `EXPR_CAST` node unless the types are already the same kind.

### `convert_to_kind(e, target_kind)` — `semantic/typecheck.c:168`

Wraps `e` in an `EXPR_CAST` node with a fresh `Type` of the given kind, unless
the expression already has that kind.  Used for integer promotions.

---

## 8. References

- C11 standard draft N1570, available at <https://port70.net/~nsz/c/c11/n1570.html>
- cppreference — [Implicit conversions](https://en.cppreference.com/w/c/language/conversion)
- cppreference — [Assignment operators](https://en.cppreference.com/w/c/language/operator_assignment)
