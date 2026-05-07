# P3 — GC root discipline in native C functions

## Problem statement

The protoCore GC traces:
- The operand stack of every active `ProtoContext` (so values pushed by bytecode are roots).
- `automaticLocals` of each `ContextScope` (so local variables in user Python frames are roots).
- Explicit roots registered via `ProtoRootSet` (e.g. active exceptions).
- The few `Cell*` fields stored in `ProtoSpace` and `ProtoSpaceImplementation`.

It does **not** trace:
- C++ stack variables in native trampoline functions.
- Function parameters (`const ProtoObject*` arguments) of native callbacks.
- Locals in C++ helper lambdas / iterators.

When a native trampoline holds a `ProtoObject*` in a C++ local across an allocation boundary (any call path that may trigger GC: bytecode execution, attribute lookup, method dispatch, cell allocation), that pointer becomes a dangling reference if the cell it pointed at gets reclaimed.

This audit inventories every native function that does this and classifies the leak path.

## The diagnostic question

For every native function:

1. Does it hold one or more `ProtoObject*` (or `Proto*` API) values in C++ locals?
2. Does it call back into the runtime in a way that may trigger GC (bytecode execution, attribute lookup, dunder dispatch, allocation)?
3. Is each held local reachable from a GC root during that callback?

The third question has three answers:

- **Yes via operand stack**: the value was pushed onto a `ProtoContext` operand stack and stays there.
- **Yes via ProtoRootSet**: the value was added to a root set (like `activeExcsRoots_` or `transientArgsRoots_`).
- **No**: the value is reachable only from C++ stack memory the GC cannot see → potential UAF.

## Inventory

### Native trampolines that loop via env->next

These are the most exposed: they iterate, each `next` call runs Python code, GC may fire mid-iteration.

| Function | File:line | Holds in C++ stack | Pinning | Status |
|---|---|---|---|---|
| `py_str_join` | `PythonEnvironment.cpp:8085` | `posArgs`, `iterable`, `it` (the iterator) | `posArgs` pinned by `invokeCallable`; `it` explicitly pinned (added in this session) | ✅ |
| `py_functools_reduce` | `FunctoolsModule.cpp:73` | `iterable`, `it`, `res` (accumulator), `func` | `posArgs` pinned by `invokeCallable`; **`it`, `res`, `func` not pinned** | ❌ HIGH |
| `py_collections_*` deque/Counter init | `CollectionsModule.cpp:426` | `iterable`, `it`, `item` | only `posArgs` pinned via invokeCallable; **the locals are not** | ❌ HIGH |
| `py_mutable_mapping_update` | `CollectionsAbcModule.cpp` (added this session) | `other`, iterator from `keys()`, etc. | `posArgs` pinned; **derived iterators not** | ❌ MEDIUM |
| `py_mapping_contains` | `CollectionsAbcModule.cpp` | self, key, val from `__getitem__` call | `posArgs` pinned; intermediate val from invokeDunder may fall through GC | ❌ MEDIUM |
| `py_repr` (`BuiltinsModule.cpp:854`) → `reprObject` | indirect | obj, items in tuple/list/dict iter | `obj` is in args; iteration is via `getIterator` which keeps `lst` alive on C++ stack only during iteration loop | ❌ MEDIUM |
| `py_dict_repr` / `py_list_repr` / `py_set_repr` etc. | `PythonEnvironment.cpp` | iterators over dict items, list elements | likely C++ stack only | ❌ MEDIUM |

**Cluster severity: HIGH.** The same UAF that caused #92's hang in `py_str_join` exists in at least 6 other native iteration sites. None is currently pinned.

### Native trampolines that callback via env->callObject

These call user-defined Python functions inside their C++ body, holding the function and intermediate values in C++ locals.

| Function | File:line | Holds | Status |
|---|---|---|---|
| `py_getattr_with_default` | `BuiltinsModule.cpp:1677` | `fn`, `args`, return value | `args` pinned via invokeCallable for the outer call only; the `fn` and intermediate values are C++ locals | LOW (single call, short window) |
| `py_max_min` reduction | `BuiltinsModule.cpp` (multiple sites) | `func`, accumulator, item from each next | not pinned | ❌ MEDIUM |
| `py_filter_next` (`BuiltinsModule.cpp:5957`) | iteration via predicate | `func`, intermediate items | not pinned | ❌ MEDIUM |
| `py_map_next` (`BuiltinsModule.cpp:6042`) | iteration via mapper | similar | not pinned | ❌ MEDIUM |

### Native operations that build and walk collections

Operations like `BUILD_LIST`, `LIST_EXTEND`, etc. that iterate input and accumulate. Most of these run in `executeBytecodeRange` where the operand stack is GC-traced, so the iterator is on the operand stack. **But the items popped during construction are NOT** — they exist briefly in C++ locals inside the opcode handler.

| Opcode | File:line | Risk |
|---|---|---|
| `OP_LIST_EXTEND` | `ExecutionEngine.cpp:5872` | `it` is C++ local; `nextVal` is C++ local. **Both unpinned.** | MEDIUM |
| `OP_BUILD_MAP` from kwargs unpack | `ExecutionEngine.cpp:6557` | iterator over `nsKeys` | MEDIUM |
| `OP_UNPACK_SEQUENCE` | `ExecutionEngine.cpp:6764, 6788, 6838` | iterator + items | MEDIUM |
| `OP_FOR_ITER` itself | `ExecutionEngine.cpp:6714` | iterator is on operand stack ✓; `val` is briefly C++ local but pushed immediately | ✅ |
| `OP_GET_ITER` | `ExecutionEngine.cpp:6695` | iterable is on operand stack ✓ | ✅ |

### Recursion through reprObject and similar

`reprObject` calls user `__repr__` which executes Python bytecode. The user code may allocate freely. Anything held in C++ locals across this call is at risk.

| Site | Risk | Status |
|---|---|---|
| `reprObject` itself | `obj` is its arg, reached from `args` of the outer C call → pinned by invokeCallable's pin. Intermediate `cls`, `reprMethod` are read-only single-step values, low risk. | ✅ mostly OK |
| `compareObjects` | similar pattern, one call out, low risk | ✅ |
| Custom `__repr__` chains (`'.'.join(repr(x) for x in items)`) | the recursion that surfaced as #92: each level holds an iterator in C++. | ❌ HIGH (the famous case) |

## Aggregated findings

### F3.1 — Most native iteration loops are unpinned

At least 6 native trampolines (`functools.reduce`, multiple `py_*_repr`, several Builtins iterators, OpcodeModule, several `PythonEnvironment` iter helpers) hold an iterator in a C++ local across `env->next` calls. Each is a UAF candidate the same way `py_str_join` was.

**Severity: HIGH** (collective). Individual incidents may be hard to reproduce because GC pressure has to align with the iteration, but they are real regressions waiting to happen.

**Fix template** (mirror the `py_str_join` pin from this session):

```cpp
proto::ProtoRootSet* roots = env ? env->getTransientArgsRoots() : nullptr;
proto::ProtoRootSet::Handle pin = proto::ProtoRootSet::kNullHandle;
if (roots && it) pin = roots->add(it);

// ... iteration loop ...

if (roots && pin != proto::ProtoRootSet::kNullHandle) roots->remove(pin);
```

Apply uniformly. Estimated effort: 30 minutes per site × 6 = 3 hours, plus verification.

### F3.2 — A reusable pin helper would reduce churn

The pattern is mechanical. A C++ RAII helper would eliminate the boilerplate AND make the discipline visible at the call site:

```cpp
class TransientPin {
    proto::ProtoRootSet* roots_;
    proto::ProtoRootSet::Handle h_;
public:
    TransientPin(PythonEnvironment* env, const proto::ProtoObject* obj)
        : roots_(env ? env->getTransientArgsRoots() : nullptr),
          h_(roots_ && obj ? roots_->add(obj) : proto::ProtoRootSet::kNullHandle) {}
    ~TransientPin() {
        if (roots_ && h_ != proto::ProtoRootSet::kNullHandle) roots_->remove(h_);
    }
    TransientPin(const TransientPin&) = delete;
    TransientPin& operator=(const TransientPin&) = delete;
};
```

Use site:
```cpp
const proto::ProtoObject* it = env->iter(iterable);
TransientPin pinIt(env, it);  // one line, RAII, panic-safe
// ... loop ...
```

**Severity: MEDIUM** (architectural cleanup). 30 minutes to introduce, then propagate.

### F3.3 — `invokeCallable` pin already covers args; doesn't cover derived locals

The fix from this session pins `args` in `invokeCallable` for native asMethod calls. This anchors the args list and everything reachable via it. But many native trampolines DERIVE values from args:

- `iterable = posArgs->getAt(0)` → reachable via args ✓
- `it = env->iter(iterable)` → for generators it == iterable ✓; for lists/dicts it's a NEW iterator NOT in args ❌
- `func = posArgs->getAt(1)` → reachable via args ✓
- intermediate values from `func(item)` → C++ locals, not pinned ❌

**Severity: HIGH** (this is the reason F3.1 exists despite the args pin).

### F3.4 — No coverage for `env->callObject` callbacks

`env->callObject(func, {arg1, arg2, ...})` runs user code. Caller holds `func`, the arg vector, and the return value in C++ locals. None of these are pinned.

The frequency:
- `BuiltinsModule.cpp`: 6 sites
- `FunctoolsModule.cpp`: 1 site
- `OpcodeModule.cpp`: 1 site
- `ExecutionEngine.cpp:2116`: native generator callback

**Severity: MEDIUM-HIGH.** Less load than iteration loops (single call vs many), but the UAF window is the entire callback duration.

**Fix**: pin `func` and each arg before the call, unpin after. Same template as F3.1.

### F3.5 — Bytecode-level opcode handlers that build collections

`OP_LIST_EXTEND`, `OP_UNPACK_SEQUENCE`, `OP_BUILD_MAP` from kwargs — each holds an iterator in a C++ local while iterating. The iterable is on the operand stack but the **iterator** isn't (`env->iter(iterable)` returns a new cell that's only in the C++ local of the opcode handler).

**Severity: MEDIUM.** Opcode handlers are short-lived but the iteration loop calls user `__next__` which can allocate freely.

**Fix**: same pattern.

### F3.6 — `args` pin in `invokeCallable` only fires for asMethod path

Looking at the current code:

```cpp
if (callable->asMethod(ctx)) {
    // pin args, call, unpin args
    return ...;
}
// other paths (Python user functions, classes, methods with __call__) — NO PIN
```

For Python user functions and class instantiation, the args list also lives in C++ locals through the runUserFunctionCall path. There the operand stack of the new context picks up the args (so it's GC-rooted from the new context). But during the brief window before the new context is set up, args is C++-only.

**Severity: LOW-MEDIUM.** The window is brief. But for completeness, the pin should cover both paths.

## Summary of P3

The native function layer of protoPython has **systematic GC root discipline gaps**. The pattern is well-defined but not enforced:

- 6+ iteration trampolines (F3.1) — HIGH
- 8+ callback sites without pin (F3.4) — MEDIUM-HIGH
- 4+ bytecode opcode handlers with internal iterators (F3.5) — MEDIUM
- The pin discipline established this session covers ~3 sites (`invokeCallable` asMethod, `py_str_join`, the `transientArgsRoots_` infrastructure). 15+ sites still need it.

**The shared root cause**: there is no policy stating "every `ProtoObject*` you hold in a C++ local across a callback must be pinned". The pattern emerged once (`py_str_join` fix) and was treated as a localised bug rather than a systemic property.

**The shared fix**: introduce `TransientPin` RAII helper (F3.2). Audit the inventory above, apply to each. Document the discipline in protoPython's CLAUDE.md.

## Action items

1. **F3.2** introduce `TransientPin` RAII helper — 30 minutes.
2. **F3.1** pin loops in `functools.reduce`, `py_collections_*`, `py_mapping_*`, `py_*_repr` — 3 hours total.
3. **F3.4** pin `env->callObject` callsites — 1-2 hours.
4. **F3.5** pin internal iterators in opcode handlers — 1 hour.
5. **F3.6** extend args pin to non-asMethod `invokeCallable` paths — 30 minutes.
6. Document the discipline in `protoPython/CLAUDE.md` so future native code follows it — 30 minutes.

Total: 6-8 hours of focused work, mostly mechanical once F3.2's helper exists.
