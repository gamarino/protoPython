# protoCore GC Bridging Rules

protoPython embeds protoCore as its object model. Whenever a `ProtoObject*` needs to outlive an allocation boundary that the protoCore tracing GC cannot see — typically because a C++ lambda registered with an HPy continuation, a thread pool, or an external event loop has captured the pointer — you MUST use one of the two protoCore-supplied mechanisms below. Smuggling references through `setAttribute` on a Python module object or a `sys.modules`-style global is an anti-pattern; do not introduce new sites that do it.

See `protoCore/DESIGN.md` § "Keeping ProtoObjects alive across allocation boundaries the GC cannot see" for the full rationale.

## Decision rule

| Object lifetime | Mechanism | Example |
|---|---|---|
| Process-perpetual (language vocabulary, prototypes, cached literals) | NULL `ProtoContext` allocation | A `co_consts`, `co_names`, `co_code` attribute key; the `__executed__` marker; an interned method name |
| Bounded async (microseconds to seconds) | `ProtoRootSet` (`PythonEnvironment::getRootSet()` or equivalent) | Native extension callbacks deferred via thread pools; `asyncio` futures held across an HPy boundary |

The two mechanisms are complementary, not interchangeable. **NEVER try to "release" a NULL-context allocation** (it has no release path; that is the whole point) and **NEVER lean on `ProtoRootSet` for objects that are conceptually language vocabulary** (you would be paying a per-cycle scan cost for no benefit).

## Mechanism A — Perpetual via NULL ProtoContext

Pass `nullptr` as the `ProtoContext*` parameter through the entire allocation call chain. The Cell goes through `posix_memalign` directly; it is never on a thread freelist or in a context's young chain, and it lives for the entire process.

```cpp
// Single-shot strong symbol — already done by createSymbol(...) for you.
const proto::ProtoString* k =
    proto::ProtoString::createSymbol(ctx, "co_consts");

// Manual perpetual allocation — only if you need a non-string Cell:
auto* permanent = new(/*ctx=*/nullptr) MyCell(/*ctor args*/);
```

**Critical invariant**: every Cell reachable from a perpetual root must itself be perpetual. A perpetual root holding a normal GC-managed reference is a use-after-free waiting to happen, because the GC sees no path to that child. In practice this means a single `nullptr` threaded through the construction call chain — `fromUTF8Bytes(nullptr, ...)` → `buildAVL(nullptr, ...)` → `new(nullptr) ProtoStringImplementation(...)`.

Where protoPython already does this for you:
* Every `ProtoString::createSymbol(ctx, name)` call is internally `is_strong=true` and routes through the perpetual path. This is what `Compiler.cpp` and `ImpModule.cpp` do when they read `co_consts`, `co_names`, `__executed__`, etc.
* Every `setAttribute(ctx, key, value)` on a heap String key auto-interns the key strongly via `SymbolTable::intern(... is_strong=true)`, also perpetual.

Don't override these — they're correct.

## Mechanism B — `ProtoRootSet` (transient pin / unpin)

For receivers of asynchronous callbacks, futures, in-flight extension-thread results — anything whose Python-side reachability ends before the C++ continuation fires — pin and release through a root set you own.

A typical pattern (mirroring what `protoJS` does in `JSContextWrapper::getRootSet()`):

```cpp
// Once at startup:
proto::ProtoRootSet* asyncRoots = space->createRootSet("protopython-async");

// At pin site:
auto cbHandle  = asyncRoots->add(callbackObj);
auto valHandle = asyncRoots->add(valueObj);

// In the C++ continuation:
externalEventLoop.enqueue([asyncRoots, cbHandle, valHandle]() {
    const proto::ProtoObject* cb  = asyncRoots->resolve(cbHandle);
    const proto::ProtoObject* val = asyncRoots->resolve(valHandle);
    asyncRoots->remove(cbHandle);
    asyncRoots->remove(valHandle);
    // dispatch...
});
```

The handle is `proto::ProtoRootSet::Handle` (a 64-bit integer with embedded generation), so capturing it by value into a C++ lambda is safe and cheap. Multiple outstanding pins are independent — a stale `remove` of a handle whose slot has been recycled is a silent no-op thanks to the generation check.

Tear down the root set in the runtime's destructor (`space->destroyRootSet(asyncRoots)`); if you forget, `~ProtoSpace` cleans up orphans.

## Anti-patterns to refuse

If you find yourself reaching for any of these in new code, STOP and use the right mechanism instead:

* `module->setAttribute(ctx, "__pending_async__", obj)` to pin a callback. Same problem as the protoJS antipattern: per-op CAS contention on a heavily mutated mutable, plus name collisions with user-set module attributes. Use a `ProtoRootSet`.
* Custom global maps `unordered_map<string, ProtoObject*>` outside protoCore's GC. Those are invisible to the tracing GC and will see use-after-free on the first collection that reclaims an entry.
* "Pin until end of program" workarounds for things that should obviously be in `createSymbol`. If it's vocabulary (an attribute name, a builtin name, a method dispatch key), intern it.
* Conditional pinning ("pin only if a thread is running") — there is no way to reliably detect GC timing, and any GC race makes this incorrect.

## HPy-specific note

When implementing C extensions through the HPy bridge, every `HPyContext`-managed handle that wraps a `ProtoObject*` is already rooted from the protoCore side as long as the HPy `Tracker` or per-call argument list keeps it alive. The bridging rules above kick in only when an extension *escapes* the synchronous HPy call boundary — for example by retaining a handle into a thread it spawns, or by registering it with a non-protoCore event loop. In that case use `ProtoRootSet`, not the HPy `Tracker` (which gets dismantled when the HPy call returns).

## Verification

When you add code that captures a `ProtoObject*` into a C++ lambda registered with any external event loop or thread pool, before merging:

1. Identify every `ProtoObject*` in the lambda's capture list.
2. For each, point at where it was either pinned via `rootSet->add(...)` or proven to be perpetual.
3. If neither, the lambda has a latent use-after-free — fix it.

## Synchronous GC root discipline for native trampolines

Native C functions implemented under `src/library/` (any `py_*` callable registered as a method on a prototype, plus the bytecode opcode handlers in `ExecutionEngine.cpp`) hold `ProtoObject*` values in C++ stack locals. The protoCore GC traces operand stacks, ContextScope automatic locals, ProtoRootSet entries — but **not** raw C++ stack memory. Any `ProtoObject*` you keep in a local across an operation that may trigger GC is a candidate UAF.

Operations that may trigger GC:

- `env->iter(x)`, `env->next(it)`, `env->callObject(fn, args)` — they run user code (`__iter__`/`__next__`/the user function) that may allocate freely.
- `invokePythonCallable(...)` and any user-method dispatch via `asMethod(...)`.
- `env->getAttribute(obj, name)` when the attribute is a property (it runs the descriptor `__get__`).
- `obj->setAttribute(ctx, name, value)` when the object is mutable (it allocates a new SparseList tree).
- Building cells: `ctx->newList()`, `ctx->newSparseList()`, `ctx->newTuple*`, `new(ctx) Whatever`, etc.

The standard helper is `protoPython::PythonEnvironment::TransientPin` (defined in `include/protoPython/PythonEnvironment.h`). RAII, panic-safe, one-line use:

```cpp
const proto::ProtoObject* it = env->iter(iterable);
PythonEnvironment::TransientPin pinIt(env, it);
for (;;) {
    const proto::ProtoObject* item = env->next(it);
    if (!item) break;
    // also pin item if you hold it across another callback:
    PythonEnvironment::TransientPin pinItem(env, item);
    const proto::ProtoObject* result = env->callObject(func, {item});
    // process result...
}
// pinIt destructor releases the pin here (also on early return / exception)
```

### Code-review checklist

For every native function you write or modify, answer:

| Question | If "yes" |
|---|---|
| Does it call `env->iter` / `env->next`? | Pin the iterator across the loop. |
| Does it call `env->callObject` or `invokePythonCallable`? | Pin every `ProtoObject*` argument and the running result. |
| Does it loop calling user code, holding an accumulator? | Pin the accumulator each iteration. |
| Does it call user `__getitem__` / `__contains__` / etc.? | Pin the receiver if it's only on the C++ stack. |
| Is the value reachable via the args list (which `invokeCallable` already pins)? | No additional pin needed. |
| Is the value reachable via the operand stack (peek-only opcode like `OP_FOR_ITER`)? | No additional pin needed. |

### Anti-patterns specific to native functions

- **Retaining a ProtoObject* across a C++ lambda or std::function callback** without an explicit pin. Same as the async patterns above; treat lambdas as escape boundaries.
- **`env->iter(x)` on a non-pinned x** — for built-in containers `iter()` returns a NEW iterator distinct from `x`, so even if x is pinned by the args list, the iterator is separate and needs its own pin. (Generators are an exception: `iter(gen) == gen`, so pinning the args list is enough — but uniformly applying TransientPin doesn't hurt.)
- **Building an iterator inside one branch and using it in another** without lifting the pin to cover both branches.

### Where the discipline already lives

- `invokeCallable` (`ExecutionEngine.cpp`): pins `args` for the duration of every native asMethod call.
- `py_str_join`, `py_reduce` (functools), `py_filter_next`, `py_map_next`, `py_mutable_mapping_update`, `py_mapping_keys`/`items`, deque init: pin their derived iterators.
- `OP_LIST_EXTEND`, `OP_UNPACK_SEQUENCE`, `OP_UNPACK_EX`: pin their internal iterators.

When adding a new native trampoline that iterates or callbacks, follow the same pattern. Audit `tasks/audit/03-gc-roots.md` lists every site we know about; add yours there if you discover a new one.
