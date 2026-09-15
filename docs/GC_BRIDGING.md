# GC bridging rules for protoCore embedders

protoPython embeds protoCore as its object model. protoCore's tracing garbage
collector finds live objects through the operand stacks of active
`ProtoContext`s, the automatic locals of each `ContextScope`, the entries of
registered `ProtoRootSet`s and a few fields of `ProtoSpace`. It does not see
`ProtoObject*` values held in raw C++ stack memory, C++ lambdas, threads,
external event loops or containers outside protoCore.

Whenever a `ProtoObject*` must stay valid across a boundary the collector
cannot see (typically because a C++ lambda registered with an HPy
continuation, a thread pool or an external event loop has captured the
pointer), use one of the two mechanisms protoCore provides, described below.
Do not keep references alive by storing them with `setAttribute` on a Python
module object or a `sys.modules`-style global, and do not add new code that
does so.

The full rationale is in protoCore's design document, section
[Keeping ProtoObjects alive across allocation boundaries the GC cannot see](https://github.com/numaes/protoCore/blob/master/DESIGN.md#keeping-protoobjects-alive-across-allocation-boundaries-the-gc-cannot-see).

## Decision rule

| Object lifetime | Mechanism | Example |
|---|---|---|
| Process-perpetual (language vocabulary, prototypes, cached literals) | Allocation with a null `ProtoContext` | A `co_consts`, `co_names` or `co_code` attribute key; the `__executed__` marker; an interned method name |
| Bounded asynchronous (microseconds to seconds) | A `ProtoRootSet` created with `ProtoSpace::createRootSet` | Native extension callbacks deferred through thread pools; `asyncio` futures held across an HPy boundary |

The two mechanisms are complementary, not interchangeable:

- A null-context allocation cannot be released. It has no release path by
  design, so never try to free one.
- Do not use a `ProtoRootSet` for objects that are conceptually language
  vocabulary. Every root set entry is scanned on every collection cycle, which
  costs time for no benefit when the object lives for the whole process anyway.

## Mechanism A: perpetual allocation with a null ProtoContext

Pass `nullptr` as the `ProtoContext*` argument through the entire allocation
call chain. The cell is then allocated directly with `posix_memalign`: it is
never placed on a thread freelist or in a context's young-generation chain, and
it lives for the entire process.

```cpp
// Single-shot strong symbol: createSymbol(...) already does this for you.
const proto::ProtoString* k =
    proto::ProtoString::createSymbol(ctx, "co_consts");

// Manual perpetual allocation, only if you need a non-string cell:
auto* permanent = new(/*ctx=*/nullptr) MyCell(/*ctor args*/);
```

**Invariant:** every cell reachable from a perpetual root must itself be
perpetual. A perpetual object that refers to an ordinary, GC-managed cell is a
use-after-free waiting to happen, because the collector sees no path to that
child. In practice this means passing a single `nullptr` down the whole
construction chain. protoCore's `SymbolTable::intern` does this: it builds each
interned string with `ProtoStringImplementation::fromUTF8Bytes(nullptr, ...)`,
which allocates the string's rope nodes with the same null context.

protoPython already relies on this mechanism in two places:

- `ProtoString::createSymbol(ctx, name)` returns a strong, interned symbol that
  is never collected. `Compiler.cpp` uses it to read `co_consts` and
  `co_names`, and `NativeModuleProvider.cpp` and `CompiledModuleProvider.cpp`
  use it for the `__executed__` marker.
- `ProtoObject::setAttribute(ctx, key, value)` interns a string key
  automatically through `SymbolTable::intern`, which is also perpetual.

Keep using these paths; they are correct.

## Mechanism B: `ProtoRootSet` (transient pin and unpin)

For the receivers of asynchronous callbacks, futures and results computed on
extension threads (anything whose Python-side reachability ends before the C++
continuation runs), pin the objects in a root set you own and release them
when you are done.

A typical pattern, mirroring protoJS's `JSContextWrapper::getRootSet()`:

```cpp
// Once at startup:
proto::ProtoRootSet* asyncRoots = space->createRootSet("protopython-async");

// At the pin site:
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

`proto::ProtoRootSet::Handle` is a 64-bit integer that encodes a slot index and
a generation, so capturing it by value in a C++ lambda is safe and cheap.
Outstanding pins are independent of each other, and removing a stale handle
whose slot has since been reused is a silent no-op because the generation no
longer matches.

Destroy the root set when the owning component shuts down
(`space->destroyRootSet(asyncRoots)`). If a root set is still registered when
the `ProtoSpace` is destroyed, the `ProtoSpace` destructor frees it.

protoPython's own root sets follow this pattern: `PythonEnvironment` creates
`activeExcsRoots_` and `transientArgsRoots_` in its constructor and destroys
them in its destructor, and `SignalModule.cpp` keeps Python signal handlers in
a root set named `signal-handlers`.

## Anti-patterns

Do not use any of the following in new code; use the matching mechanism above
instead.

- `module->setAttribute(ctx, "__pending_async__", obj)` to pin a callback. It
  causes compare-and-swap contention on a heavily mutated object and can
  collide with attributes that user code sets on the module. Use a
  `ProtoRootSet`.
- Custom global maps such as `std::unordered_map<std::string, ProtoObject*>`
  outside protoCore. The tracing collector cannot see them, so an entry becomes
  a dangling pointer as soon as a collection reclaims the object it refers to.
- "Pin until the end of the program" workarounds for values that belong in
  `createSymbol`. If the value is vocabulary (an attribute name, a builtin
  name, a method dispatch key), intern it.
- Conditional pinning, such as "pin only if a thread is running". There is no
  reliable way to predict when a collection happens, so any race with the
  collector makes this incorrect.

## HPy extensions

The HPy bridge keeps its handles in `HPyContext::handles`, a
`std::vector<const proto::ProtoObject*>` (see
`include/protoPython/HPyContext.h`). Although the comment in that header
describes handles as roots, the vector is not registered with a `ProtoRootSet`,
so do not rely on a handle to keep an object alive across a collection.

During a synchronous call, objects passed as arguments stay reachable through
the argument list, which `invokeCallable` pins for the duration of every native
call (see the next section). The rules above apply as soon as an extension lets
an object escape that call: for example, by keeping a handle in a thread it
spawns or registering it with an event loop that is not driven by protoCore. In
that case, pin the object in a `ProtoRootSet`.

## Verification for asynchronous captures

Before merging code that captures a `ProtoObject*` in a C++ lambda registered
with an external event loop or thread pool:

1. Identify every `ProtoObject*` in the lambda's capture list.
2. For each one, point to where it is pinned with `rootSet->add(...)` or show
   that it is perpetual.
3. If you can do neither, the lambda has a latent use-after-free; fix it before
   merging.

## Synchronous GC root discipline for native functions

Native functions under `src/library/` (every `py_*` callable registered as a
method on a prototype, plus the bytecode opcode handlers in
`ExecutionEngine.cpp`) hold `ProtoObject*` values in C++ stack locals. The
collector traces operand stacks, `ContextScope` automatic locals and
`ProtoRootSet` entries, but not raw C++ stack memory. Any `ProtoObject*` kept
in a local across an operation that may trigger garbage collection can become
a dangling pointer.

Operations that may trigger garbage collection:

- `env->iter(x)`, `env->next(it)` and `env->callObject(fn, args)`: they run
  user code (`__iter__`, `__next__`, the called function), which may allocate
  freely.
- `invokePythonCallable(...)` and any dispatch to a user method through
  `asMethod(...)`.
- `env->getAttribute(ctx, obj, name)` when the attribute is a property, because
  it runs the descriptor's `__get__`.
- `obj->setAttribute(ctx, name, value)` when the object is mutable, because it
  allocates a new sparse-list tree.
- Building cells: `ctx->newList()`, `ctx->newSparseList()`,
  `ctx->newTuple(...)`, `new(ctx) Whatever` and similar calls.

The standard helper is `protoPython::PythonEnvironment::TransientPin`, defined
in `include/protoPython/PythonEnvironment.h`. It is an RAII guard that pins the
object in the environment's transient root set and releases the pin when it
goes out of scope, including on early return or exception. Pinning a null
pointer is a no-op.

```cpp
const proto::ProtoObject* it = env->iter(iterable);
PythonEnvironment::TransientPin pinIt(env, it);
for (;;) {
    const proto::ProtoObject* item = env->next(it);
    if (!item) break;
    // Also pin item if you hold it across another callback:
    PythonEnvironment::TransientPin pinItem(env, item);
    const proto::ProtoObject* result = env->callObject(func, {item});
    // process result...
}
// pinIt's destructor releases the pin here (also on early return or exception).
```

### Code-review checklist

For every native function you write or modify, answer these questions:

| Question | If the answer is yes |
|---|---|
| Does it call `env->iter` or `env->next`? | Pin the iterator across the loop. |
| Does it call `env->callObject` or `invokePythonCallable`? | Pin every `ProtoObject*` argument and the running result. |
| Does it loop calling user code while holding an accumulator? | Pin the accumulator on each iteration. |
| Does it call a user `__getitem__`, `__contains__` or similar method? | Pin the receiver if it is only held on the C++ stack. |
| Is the value reachable through the argument list, which `invokeCallable` already pins? | No additional pin is needed. |
| Is the value reachable through the operand stack, as in a peek-only opcode such as `OP_FOR_ITER`? | No additional pin is needed. |

### Anti-patterns in native functions

- **Keeping a `ProtoObject*` in a C++ lambda or `std::function` callback**
  without an explicit pin. This is the same problem as the asynchronous cases
  above: treat lambdas as escape boundaries.
- **Calling `env->iter(x)` and holding the result unpinned.** For built-in
  containers `iter()` returns a new iterator distinct from `x`, so even when
  `x` is pinned through the argument list, the iterator needs its own pin.
  Generators are an exception (`iter(gen)` is `gen`), but applying
  `TransientPin` uniformly does no harm.
- **Creating an iterator in one branch and using it in another** without
  extending the pin to cover both branches.

### Where the discipline is already applied

- `invokeCallable` (`ExecutionEngine.cpp`) pins the argument list for the
  duration of every native `asMethod` call.
- `py_str_join` (`PythonEnvironment.cpp`), `py_reduce`
  (`FunctoolsModule.cpp`), `py_sum`, `py_all`, `py_any` and `py_sorted`
  (`BuiltinsModule.cpp`), `py_mapping_keys`, `py_mutable_mapping_update` and the
  helper behind `py_mapping_items` (`CollectionsAbcModule.cpp`), and
  `deque_collect`, which fills a deque from an iterable
  (`CollectionsModule.cpp`), pin the iterators they create.
- `py_filter_next` and `py_map_next` (`BuiltinsModule.cpp`) pin the values they
  hold across calls to the user function.
- The `OP_LIST_EXTEND`, `OP_DICT_UPDATE`, `OP_UNPACK_SEQUENCE` and
  `OP_UNPACK_EX` handlers (`ExecutionEngine.cpp`) pin their internal iterators.

When you add a native function that iterates or calls back into Python, follow
the same pattern. The audit in [docs/audits/03-gc-roots.md](audits/03-gc-roots.md)
inventories the sites found so far; if you find a new one, add it there.
