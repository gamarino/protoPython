# P2 — Fast paths that may bypass dunder dispatch

## Problem statement

protoPython's bytecode interpreter and many of its native C trampolines have *fast paths* that recognise common built-in types (`int`, `float`, `str`, `list`, `tuple`) and execute the operation directly with C primitives, bypassing the dunder protocol. These are correct **when the operand is the literal built-in type**, but become silent semantic bugs when the operand is a Python subclass that overrides the operation.

The pattern was first surfaced as #89 (`int` subclass `__add__` ignored). This audit inventories every recurrence so the same fix can be applied uniformly.

## The diagnostic question

For every fast path of the form `if (x->isXxx()) { return primitive_op(...); }`:

> If a Python user defines `class S(Xxx): def __op__(self, ...): return special`, and calls `S() op other`, does this fast path execute and silently return the wrong result?

When the answer is yes → **HIGH** severity (silent semantic bug).
When the answer is "raises TypeError visible to user" → **MEDIUM** (loud failure, easier to diagnose).
When the result is identical to dunder dispatch → **LOW** (an optimisation that happens to be correct).

## Inventory by category

### A. Binary arithmetic operators (`ExecutionEngine.cpp`)

The pattern: `if ((aa.isInteger || aa.isDouble) && (bb.isInteger || bb.isDouble)) return aa->op(bb);`. Bypasses `__op__`/`__rop__` when either operand is a Python subclass.

| Site | Status | Severity |
|---|---|---|
| `binaryAdd` (line 1230) | ✅ **Fixed in #89**: now checks `type(a) == int/bool/float` exactly before fast path. | (resolved) |
| `binarySubtract` (line 1340) | ❌ Same anti-pattern, not fixed. | HIGH |
| `binaryMultiply` (line 1351) | ❌ Same anti-pattern, not fixed. Plus has additional fast paths for `str * int` and `tuple * int` that may also bypass `__mul__`/`__rmul__` overrides. | HIGH |
| `binaryUnaryNegative` (line 1421) | ❌ `if (a.isInteger) return a->multiply(-1)` — bypasses `__neg__`. | HIGH |
| `binaryTrueDivide` (line 1428) | ❌ Same anti-pattern. | HIGH |
| `binaryModulo` (line 1474) | ❌ Same anti-pattern. (Has special handling for `str % args` to delegate to `__mod__`, but int/float subclass with overridden `__mod__` is bypassed.) | HIGH |
| `binaryPower` (line 1527) | ❌ Same anti-pattern. | HIGH |
| `binaryFloorDivide` (line 1571) | ❌ Same anti-pattern. | HIGH |

**Cluster severity: HIGH.** 7 of 8 binary arithmetic operators have the same bug as the one fixed in #89. A user class extending `int` or `float` to override any of these ops silently fails. Likely to surface as test_descr / test_types failures and as user-visible bugs in stdlib code that uses Fraction/Decimal-style subclasses.

**Fix template** (mirror #89 in `binaryAdd`):
```cpp
bool aPrim = false, bPrim = false;
if (env && (aa->isInteger(ctx) || aa->isDouble(ctx))) {
    const proto::ProtoObject* aCls = env->getType(ctx, a);
    aPrim = (aCls == env->getIntPrototype()
             || aCls == env->getBoolPrototype()
             || aCls == env->getFloatPrototype());
}
// same for bPrim, then guard the fast path:
if (aPrim && bPrim) return aa->op(ctx, bb);
```

Plus subclass `__rop__` priority: when `type(b)` is a proper subclass of `type(a)` and overrides `__rop__`, CPython tries `b.__rop__(a)` *before* `a.__op__(b)`. Currently `binaryOpDispatch` does not do this. Tracked separately under **F2.5** below.

### B. Bytecode-level fast paths in `executeBytecodeRange`

| Opcode | Site (`ExecutionEngine.cpp`) | Bypass | Severity |
|---|---|---|---|
| `OP_BINARY_ADD` | line 3443: `if (isSmallInt(a) && isSmallInt(b)) return SmallInt sum` | bypasses `int.__add__` override on subclasses | (relies on the `binaryAdd` fix above for the slow path; the SmallInt fast path itself doesn't dispatch dunders, but SmallInt is by construction always the literal `int` — never a subclass instance). ✅ low risk |
| `OP_INPLACE_ADD` | line 3465: same SmallInt fast path | same | ✅ low risk |
| `OP_BINARY_SUBTRACT` | line 3493: same SmallInt fast path | same | ✅ low risk |
| `OP_BINARY_MULTIPLY` | line 3538: same SmallInt fast path (followed by call to `binaryMultiply` which is broken — see A) | dispatches to broken `binaryMultiply` | depends on A |
| `OP_COMPARE_OP` | line 4419: `if (isSmallInt(a) && isSmallInt(b))` directly compares C longs | bypasses `__eq__`/`__lt__`/etc. — but again SmallInt instances *are* the literal `int`, never subclasses | ✅ low risk for SmallInt path; the slow path delegates to `compareOp` which itself needs auditing |
| `OP_BINARY_SUBSCR` | line 5316: `if (isSmallInt(key) && container is list)` — direct `list->getAt(idx)` | **bypasses `__getitem__` override on `list` subclasses** | HIGH |
| `OP_STORE_SUBSCR` | line 5455: similar `list[smallint] = value` fast path | **bypasses `__setitem__` override on `list` subclasses** | HIGH |
| `OP_CONTAINS` (the `in` operator, inside `compareOp`) | `ExecutionEngine.cpp` lines 1622-1740 | ✅ **Fixed in #90**: priority dispatch when `type(b).__mro__` defines `__contains__`. | (resolved, but only for tag-non-OBJECT containers — see F2.4) |
| `OP_GET_ITER` / `OP_FOR_ITER` | line 6695, 6714 | always go through `env->iter`/`env->next` which dispatch dunders | ✅ correct |

**Cluster severity: MIXED.** SmallInt fast paths are safe by construction (SmallInt is always `int`, not a subclass). List subscript fast paths (HIGH severity, two sites) are not.

### C. Container fast paths

| Operation | Site | Bypass | Severity |
|---|---|---|---|
| `OP_CONTAINS` (`in`) → list/tuple iteration | `ExecutionEngine.cpp:1700-1719` | walks the underlying ProtoList/ProtoTuple even when `b` is a user subclass; partial fix #90 only fires when `type(b).__mro__[*]` has own `__contains__` | covered by #90 — verify no regressions on subclasses inheriting `__contains__` from base |
| `OP_CONTAINS` `in dict` | `ExecutionEngine.cpp:1672-1697` | walks the SparseList carrier directly | covered by #90 priority dispatch |
| `len(x)` (inside `py_len` builtin) | `BuiltinsModule.cpp` | inspects internal carriers (asList, asString, asTuple) | needs check whether subclass `__len__` overrides are honoured |
| `bool(x)` truthiness | `isTruthy` in ExecutionEngine | bypasses `__bool__` for built-in types | needs check |
| `iter(x)` | `env->iter` | dispatches via `__iter__` lookup | ✅ correct, sample reviewed |

### D. Native method fast paths in PythonEnvironment.cpp

The `py_int_arith` / `py_str_*` / `py_list_*` family functions have entry guards like `if (self->isInteger) ...`. These are typically dispatch endpoints for the dunder itself, so the "subclass bypass" question is inverted: `int.__add__(self=I, other=...)` is correctly called for `I` regardless of whether `I` is `int` or a subclass — the question is whether the implementation handles the case where `other` is a non-primitive numeric (a subclass with `__radd__`).

`py_int_arith` was the site of the recursion bug fixed in #89 (it called `env->binaryOp` which re-entered the broken fast path).

| Site | Issue | Status |
|---|---|---|
| `py_int_arith` (line 4843) | Was calling `env->binaryOp` recursively; replaced with direct `add`/`subtract`/etc. | ✅ fixed in #89 |
| Other `py_*_arith` for str/list/tuple | not audited yet — sample shows similar patterns | LOW-MEDIUM (dunders rarely overridden for those) |

### E. Implicit `__bool__` / `__len__` / `__hash__`

Truthiness, length, and hashing have type-driven fast paths. CPython's spec is precise about when subclass `__bool__` is consulted (always, if defined). protoPython's `isTruthy` and `getHash` likely have built-in optimisations.

| Op | Site | Risk | Severity |
|---|---|---|---|
| `isTruthy(x)` for non-zero numeric / non-empty container | various | bypasses user `__bool__` | LOW unless user subclasses |
| `getHash(x)` for string/int/tuple | `ProtoObject::getHash` (protoCore) | bypasses user `__hash__` for built-ins (usually correct) | LOW |
| `__hash__ = None` in user class | `py_hash` | should raise TypeError; #88's fix added this | ✅ fixed |

### F. Special-method lookup — descriptor protocol

`reprObject`, `compareObjects`, etc. look up `__repr__`/`__eq__` on `type(obj)`. The proto-prototype chain walk has historically diverged from Python's `__mro__` (fixed in #88 for `reprObject`, but other call sites may still walk the wrong chain).

| Site | Walks | Status |
|---|---|---|
| `reprObject` | now walks `__mro__` | ✅ fixed in #88 |
| `compareObjects` | unaudited | needs check |
| `py_object_eq` / `py_object_ne` | walks parent chain | needs check |
| `super().X` | walks `__mro__` from `type` argument | mostly fixed in #93 |

## Aggregated findings

### F2.1 — 7 binary arithmetic ops have the same anti-pattern as #89

`binarySubtract`, `binaryMultiply`, `binaryUnaryNegative`, `binaryTrueDivide`, `binaryModulo`, `binaryPower`, `binaryFloorDivide` all bypass `__op__`/`__rop__` when the operand is a Python subclass of int/float/bool with override.

**Severity: HIGH.** Mechanical fix; mirror the `binaryAdd` template across all 7.
**Estimated effort: 1-2 hours.**

### F2.2 — `OP_BINARY_SUBSCR` and `OP_STORE_SUBSCR` bypass `list` subclass `__getitem__`/`__setitem__`

The `list[smallint]` fast paths execute `list->getAt(idx)` / `list->setAt(idx, value)` directly without checking whether `type(container)` is `list` exactly or a subclass.

**Severity: HIGH.** Same fix template: gate the fast path with `type(container) == listPrototype`.
**Estimated effort: 30 minutes.**

### F2.3 — Subclass `__rop__` priority not implemented

CPython rule: when `type(b)` is a proper subclass of `type(a)` and overrides `__rop__`, `b.__rop__(a)` is tried *before* `a.__op__(b)`. `binaryOpDispatch` always tries `a.__op__` first.

**Severity: MEDIUM** (only matters when both operands are different classes in an inheritance relationship; rare in practice but causes surprising bugs).
**Estimated effort: 1 hour.**

### F2.4 — `OP_CONTAINS` priority dispatch (#90 fix) only fires for non-built-in containers

The fix in #90 explicitly excludes `dict`/`list`/`tuple`/`set`/`frozenset`/`str`/`bytes` from the priority dispatch. So a user class that subclasses `list` and overrides `__contains__` falls through to the list fast path and is bypassed. Same anti-pattern as F2.2.

**Severity: MEDIUM** (subclassing built-in containers with `__contains__` override is uncommon).
**Estimated effort: 30 minutes** (relax the gate, mirror F2.2).

### F2.5 — `compareObjects` / `py_object_eq` walk the proto-parent chain instead of `__mro__`

The pattern from #88 (default object `repr` walking the wrong chain) likely recurs in equality, comparison, hashing. Each operator that does dunder lookup needs to walk Python `__mro__`, not the protoCore parent chain (which contains the metaclass `type` instead of the user class's bases).

**Severity: MEDIUM-HIGH.** Hard to detect because tests rarely exercise the metaclass MRO divergence. The same fix template as #88 applies.
**Estimated effort: 1-2 hours** to audit each of `__eq__`/`__ne__`/`__lt__`/`__hash__` lookup sites.

### F2.6 — Truthiness and length fast paths are unaudited

`isTruthy(x)` is called per-comparison and per-jump. If it has a built-in fast path that bypasses `__bool__`, every loop guard / conditional with a custom-truthy class is wrong.

**Severity: TBD** until inspected.
**Estimated effort: 30 minutes** to inspect.

## Summary of P2

The "fast path bypasses dunder" pattern is **systemic, not isolated**. Counts:

- 7 binary arithmetic ops (F2.1) — HIGH
- 2 subscript ops (F2.2) — HIGH
- 1 missing CPython rule (F2.3) — MEDIUM
- 1 partial fix to extend (F2.4) — MEDIUM
- An unknown number of dunder-lookup sites that walk the wrong chain (F2.5) — MEDIUM-HIGH
- Truthiness / length / hash sites unaudited (F2.6) — TBD

**The shared root cause**: every fast path is written as "if X is one of the built-in types, do C; else fall through to Python dunder". The check `is one of the built-in types` was implemented as `isInteger() / isString() / etc.` which is true for both the literal type AND any subclass. The correct check is `type(x) is the literal prototype`.

**The shared fix template**: introduce a helper `isExactly(env, x, prototype)` that checks `type(x) == prototype` (no subclass) and use that as the fast-path gate. Apply uniformly. Estimated total effort: 4-7 hours.

## Action items

1. **F2.1 fix** — apply `binaryAdd`-style guard to the other 7 arithmetic ops. (1-2h)
2. **F2.2 fix** — gate `OP_BINARY_SUBSCR` and `OP_STORE_SUBSCR` list fast paths with exact-type check. (30min)
3. **F2.4 fix** — extend `OP_CONTAINS` priority dispatch to fire for built-in container subclasses too. (30min)
4. **F2.3 fix** — implement subclass `__rop__` priority in `binaryOpDispatch`. (1h)
5. **F2.5 audit** — inspect every `__eq__`/`__hash__`/`__lt__`/etc. lookup site for proto-chain vs MRO walk. (1-2h to audit, fix scope TBD)
6. **F2.6 audit** — inspect `isTruthy` and length fast paths. (30min)

Total: 4-7 hours of focused work, mostly mechanical once the template is known.

## Architectural recommendation

Add a `proto/python_dispatch.h` header (or similar) that encapsulates the "dispatch a binary op respecting subclass overrides" logic with a single helper:

```cpp
template<class Primitive>
const proto::ProtoObject* dispatchBinaryNumericOp(
    PythonEnvironment* env,
    proto::ProtoContext* ctx,
    const proto::ProtoObject* a,
    const proto::ProtoObject* b,
    const char* dunder, const char* rdunder,
    Primitive primitiveFn);
```

Every call site (currently 7 with the bug pattern) should reduce to one line. The helper internally:

1. Computes `type(a)` and `type(b)`.
2. If both are exact built-ins → run primitive.
3. If only one is a subclass and overrides reflected op → try reflected first.
4. Else dispatch via `__op__`/`__rop__`.

This matches CPython's PyNumber_BinaryOp logic and eliminates the recurrence pattern at its source.
