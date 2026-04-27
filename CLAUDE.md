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
