# Design: delegate Python attribute access to protoCore

**Date:** 2026-04-28
**Status:** Design document — implementation in phases.
**Context:** Profile of bench_richards_lite.py shows ~30 `getAttribute`
calls per Python source line, when CPython's specialised LOAD_ATTR /
LOAD_METHOD opcodes do 0–2.  The asymmetry is not in the cost per
lookup (protoCore's per-thread cache delivers ~1.7 ns per hit, on par
with CPython's inline cache) but in **how many lookups protoPython
performs to resolve a single Python operation**.

## TL;DR — the architectural error

`PythonEnvironment::getAttribute` is **645 lines long** and
re-implements:
  - prototype-chain walking;
  - Python MRO traversal via `__mro__`;
  - `__class__` lookups to resolve the type;
  - descriptor protocol (`__get__` / `__set__`) for every read;
  - `__getattribute__` user-defined hook dispatch;
  - `__dict__` / `__mro__` / `__class__` special cases…

…on top of `proto::ProtoObject::getAttribute`, which **already** walks
the prototype chain (linearised parents) and hits a per-thread cache
on the way.  protoPython is doing the same work twice.

The class hierarchy is already represented correctly in protoCore:
  - A Python class `C` is a `ProtoObjectCell` whose **parents** are
    the base classes in MRO order (linearised by C3, attached via
    `addParent`).
  - A Python instance `obj = C()` is a `ProtoObjectCell` constructed
    via `cls->newChild(ctx, true)` — its **parent** is the class.
  - Attribute reads on `obj.attr` should be a single
    `obj->getAttribute(ctx, name)` call: protoCore walks instance →
    class → bases in C3 order, hits the per-thread cache on every
    step, returns the resolved attribute.

But `PythonEnvironment::getAttribute` does NOT call
`obj->getAttribute(ctx, name)` as the default path.  It walks
explicitly: read `__class__`, read `__mro__`, iterate the MRO list,
on each entry check `__data__`, etc.  Each of those is its own
`getAttribute` call.

Result: ~30 `getAttribute` calls per Python line.  Cache works fine,
but you can't out-cache the wrong number of lookups.

## Why CPython does 0–2 per opcode

CPython 3.11+ has specialised opcodes:
  - `LOAD_ATTR_INSTANCE_VALUE`: validates `obj.__class__`'s shape
    pointer (1 deref), reads the value at the cached offset (1 deref).
    Done.  No chain walk.
  - `LOAD_METHOD`: same as above but skips the bound-method allocation;
    the matching `CALL_METHOD` invokes the unbound function directly.
  - `IS_OP`, `COMPARE_OP_INT`: compare without going through
    `__eq__`/`__class__` lookups.

The inline cache is keyed by `(opcode_address, shape_ptr)`.  After
warmup, the path is essentially branch-predicted and reduces to a
single dependent load.

## What we have today

Class construction (`runUserClassCall`):
```cpp
obj = ctx->newObject(true);
obj = obj->addParent(ctx, self);                  // ← chain wired
obj = obj->setAttribute(ctx, "__class__", self);  // ← redundant
```

Instance construction (`py_set_call`, `py_object_new`, …):
```cpp
instance = cls->newChild(ctx, true);              // ← parent = class
instance->setAttribute(ctx, "__class__", cls);    // ← redundant
instance->setAttribute(ctx, "__data__", innerObj);// ← wrapper indirection
```

Attribute access (`PythonEnvironment::getAttribute`, 645 LOC):
```cpp
objClass = this->getType(ctx, obj);     // ← getAttribute(__class__) hop
isModule = (objClass == this->modulePrototype);
isClass = this->isActuallyAClass(ctx, obj);  // ← multiple getAttribute calls
… 645 lines of MRO walking, descriptor dispatch, special cases …
```

The chain is wired; nothing reads through it.

## Target design

### Hot path (the 99% case)

```cpp
const proto::ProtoObject* PythonEnvironment::getAttribute(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* obj,
        const proto::ProtoString* name,
        bool raiseError, bool* outIsUnboundFunc) {

    // FAST PATH — pure protoCore prototype-chain walk.  Hits the
    // per-thread attribute cache.  Walks instance → class → bases
    // in the order C3 linearised at class-construction time, exactly
    // as Python expects.
    const proto::ProtoObject* result = obj->getAttribute(ctx, name);

    // 99% of the time, the result is the resolved attribute.  Done.
    if (result && !needsDescriptorProtocol(result) &&
        !isPythonGetattributeOverridden(ctx, obj)) {
        return result;
    }

    // SLOW PATH — descriptor protocol, __getattribute__ hook, raise
    // AttributeError.  Same as today's 645-line function.
    return slowAttributeLookup(ctx, obj, name, raiseError, outIsUnboundFunc);
}
```

### Synthesised attributes

`__class__`, `__mro__`, `__dict__`, `__bases__` are the only
attributes that need special handling because they expose the chain
itself.  None of them is read on the hot path (richards_lite never
reads `__mro__` directly), but they're part of the language.  Strategy:

  - Don't store them.  Synthesise on demand inside the
    `slowAttributeLookup` (or a thin shim before it).
  - `obj.__class__` → `obj->getFirstParent(ctx)` (single protoCore
    call, no extra storage).
  - `cls.__mro__` → walk parents recursively, collect into a tuple.
    Cache the result via `setAttribute(ctx, "__mro_cache__", ...)`
    on the class itself (it's immutable after class creation; safe
    to cache).
  - `obj.__dict__` → enumerate `obj->getOwnAttributes(ctx)` and wrap
    in a Python `dict`.  Synthesised every call (a snapshot, like
    CPython's `vars(obj)` proxy).
  - `obj.__bases__` → walk the immediate parents (one level).

### Descriptor protocol

The slow path stays for descriptors (`@property`, `@classmethod`,
`__set_name__`, …) and user-defined `__getattribute__`.  Detection:
  - The fast-path's protoCore lookup may return a function/method
    that's actually a descriptor (e.g. a `property` object).  After
    the lookup, check if the result has `__get__`; if so, fall to the
    descriptor invocation in the slow path.
  - Pre-class metadata: when a class is built with any descriptors in
    its body, set a flag attribute `__has_descriptors__ = True`.  Fast
    path skips the post-check on classes without that flag.

### `__data__` indirection

Today many native types (str, list, set, dict) wrap their inner
storage in a `__data__` attribute on the Python instance:
```cpp
b->setAttribute(ctx, "__data__", actualBuffer);
```
This is fine for native types whose storage is a separate native cell.
But for user-defined classes there's no `__data__`; their attributes
are direct on `obj`.  The fast path doesn't need to know about
`__data__` — protoCore's chain walk finds direct attributes first;
only native types implementing dunder methods touch `__data__`, and
those flow through the slow path (descriptor invocation).

## Phased migration

### Phase 1 — instrument and prove the win

Add an alternate fast-path entry in `PythonEnvironment::getAttribute`
that's **opt-in via env var** (`PROTOPY_FAST_GETATTR=1`).  When set:
  1. Try `obj->getAttribute(ctx, name)`.
  2. If non-null and not a descriptor, return it.
  3. Else fall to today's slow path.

Run the pyperf suite both ways.  Measure the headline geomean and
per-benchmark ratio.  This isolates the architectural change from the
risk of breaking corner cases — the slow path is unchanged, the new
path is a strict additive optimisation toggled at runtime.

Expected result: a substantial drop in geomean (target ≤ 30×),
zero regressions when the flag is off.

### Phase 2 — make the fast path the default

Once Phase 1 demonstrates the win and the test suite passes with the
flag on, flip the default.  The slow path is still the fallback for
descriptor protocol, `__getattribute__` overrides, `super()` proxies,
and the `__class__` / `__mro__` synthesis.

### Phase 3 — synthesise __dict__, __mro__, __class__

Replace the stored `__class__` / `__mro__` attributes (set in
class/instance construction) with synthesis from the chain.  Drop the
redundant `setAttribute("__class__", cls)` calls in
`runUserClassCall`, `py_set_call`, `py_object_new`, etc.  Slow path
recognises these names and computes the result lazily.

After this phase, attribute storage on Python objects is exactly the
user's `__dict__` — no internal bookkeeping mirroring.

### Phase 4 — eliminate `isActuallyAClass` from hot path

`isActuallyAClass(ctx, obj)` is currently called once per
`getAttribute` (~3–5 times per Python line).  After Phase 3 the test
collapses to: "`obj` has `typePrototype` in its parent chain".  That
becomes a single inline `obj->hasParent(ctx, typePrototype)` — and
once the parent chain is the source of truth, this can also be cached
per-object (set once at class-construction time).

### Phase 5 — measure and iterate

Re-run the pyperf suite after each phase, update README baseline,
identify whatever new bottleneck the phase exposes (probably bytecode
dispatch overhead — `executeBytecodeRange` 7%, `runUserFunctionCallRaw`
1.5%).  Tier B in the older performance analysis (LOAD_METHOD /
CALL_METHOD opcodes) becomes the natural next target.

## Risks and how to mitigate them

  - **Descriptor detection false negatives**: if the fast path returns
    something that should have invoked `__get__` but didn't, the user
    sees the wrong value.  Mitigation: every result that's an instance
    of `property`, `classmethod`, `staticmethod`, or a class with
    `__get__` defined goes through the slow path.  The fast path's
    "is a descriptor" test is conservative.
  - **`__getattribute__` overrides**: any class defining
    `__getattribute__` must NOT use the fast path on its instances —
    Python guarantees the user hook runs first.  Detect at class-
    construction time, set a flag on the class, fast path checks it.
  - **Test coverage**: run the full CPython conformance test suite
    (`test/conformance/`) under both code paths; require zero
    regressions before flipping defaults.
  - **`super()` proxies**: super's attribute access has its own dance
    that bypasses the normal chain.  Keep that in the slow path; the
    fast path checks for the `__is_super_proxy__` marker first.

## Out of scope for this design

  - Inline caching at the bytecode site (LOAD_ATTR_INSTANCE_VALUE
    style).  Belongs in Tier B; orthogonal to this delegation work.
  - Hidden classes / shape transitions.  Tier C.
  - JIT.

## Why this matters

The user pointed out the asymmetry: "no entiendo el 64×.  Acceder a
un atributo debe hacerse usando la base de protoCore como respaldo,
no creando en protoPython un walkthroug de la cadena de clases y
herencias".  This document captures that diagnosis as a concrete
architectural plan.

The diagnosis is correct: protoPython has been doing the chain walk
twice — once in protoCore (correctly, with a per-thread cache) and
once in itself (wrongly, with no cache, traversing `__mro__` lists
and `__class__` references).  Phase 1 alone is expected to bring the
pyperf geomean from ~65× into the low-30s by collapsing the 30
getAttribute calls per line into 1–3.

Phases 2–4 retire the parallel bookkeeping (`__class__`, `__mro__`,
`__data__` mirroring) and let the prototype chain be the single
source of truth for attribute resolution, the way protoCore was
designed for.  The slow path remains for the actual semantic
features that need it (descriptors, user-defined `__getattribute__`,
`super()`).
