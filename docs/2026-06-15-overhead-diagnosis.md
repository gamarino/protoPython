# protoPython overhead — diagnosis & optimisation plan

**Date**: 2026-06-15.
**Hardware**: Ryzen 5500U (6 cores, mobile), Linux x86_64.
**Binary measured**: `build-lto/src/runtime/protopy`.
**Tools**: `perf stat -r 3`, `perf record -F 999`, cross-referenced with the protoJS optimisation history (P-JS-0..7).

The motivating data is the protoCpp benchmark suite at `gamarino/protoCpp`:
the same C++ workloads driven directly against protoCore beat CPython on
5 of 6 microbenches, while protopy *loses* to CPython on 5 of 6 of those
SAME benches. Since both stand on the same `libprotoCore.so`, the gap is
the Python layer. This document quantifies where the gap lives and
ranks the work to close it.

## Where the cycles go

Three perf profiles, three different bottlenecks. Workloads are the
protoPython bench suite (`benchmarks/*.py`) — fib(25), 100 K × 3-attribute
read, 10 K list-append.

### `call_recursion` (fib 25) — call-path overhead

```
 10.14 %  proto::ProtoContext::ProtoContext      <-- fresh context per Python call
  9.13 %  protoPython::executeBytecodeRange      <-- bytecode dispatch self-time
  2.47 %  proto::ProtoObject::getAttribute       <-- kernel attribute lookup
  2.07 %  proto::isObject                        <-- tag check
  1.65 %  proto::ProtoSparseListImplementation::implGetAt
  1.38 %  __tls_get_addr                         <-- TLS access
  0.87 %  PythonEnvironment::hasPendingException <-- per-opcode exception check
  0.84 %  runUserFunctionCallRaw                 <-- the call dispatch itself
  0.66 %  protoPython::getPyThread               <-- TLS-based pyThread lookup
  0.55 %  protoPython::diagEnabled               <-- debug flag check
```

**Reading**: roughly **25 % of fib(25) goes to per-Python-call setup** (context construction + dispatch entry + TLS lookups + state probes). The recursive body itself — adding two integers and comparing — is doing about 1/4 of the work; the rest is housekeeping. fib(25) makes 121 K recursive calls, so a 10 % cost on the context constructor means the constructor is ~140 ns per call.

> The ExecutionEngine source already acknowledges this at line 362-364: *"Profiles of call_recursion (fib(25), 242k calls) showed 90% of time in ProtoContext destruction + cell free-list refilling; frame setup was the upstream amplifier."* The fact that the cost is still 10 % means the existing SBO optimisation in `MemoryManager::ContextScope` (64-slot inline buffer) is not enough — see #1 below.

### `attr_lookup` — bytecode dispatch dominates

```
 26.70 %  protoPython::executeBytecodeRange      <-- dispatch loop self-time
  9.33 %  proto::ProtoObject::getAttribute
  6.60 %  proto::isObject
  5.89 %  proto::ProtoSparseListImplementation::implGetAt   <-- inside getAttribute
  5.80 %  PythonEnvironment::hasPendingException
  3.48 %  proto::toImpl<ProtoObjectCell>         <-- downcast helper
  2.57 %  __tls_get_addr
  2.53 %  getPyThread
  2.12 %  diagEnabled
  1.36 %  proto::ProtoSparseListImplementation::implHas
```

**Reading**: when the body of every iteration is *fast* (three attribute reads + three integer adds), the **dispatch loop itself becomes 27 % of the runtime**. That is the highest-leverage target. About 22 % more goes to the kernel attribute path (`getAttribute` + `implGetAt` + `isObject` + tag checks); 11 % to per-opcode bookkeeping (`hasPendingException` + `diagEnabled` + TLS lookups).

### `list_append_loop` — allocator / GC pressure

```
  6.96 %  proto::gcThreadLoop                    <-- the concurrent GC thread
  6.50 %  proto::ProtoSpace::getFreeCells        <-- slow-path allocation
  4.85 %  proto::ProtoListImplementation::processReferences   <-- GC trace
  3.28 %  proto::ProtoObject::isCellPointer
  2.82 %  ProtoListImplementation::ProtoListImplementation
  2.62 %  proto::compareTuples
  2.56 %  proto::ProtoContext::allocCell
  1.88 %  proto::ProtoObject::asCellPointer
  1.59 %  __memmove_avx_unaligned_erms
```

**Reading**: `list_append_loop` is dominated by GC pressure. Every append builds a fresh persistent list — `appendLast` on an AVL-backed structure — which costs `O(log N)` cell allocations per call. With 10 K appends, the working set forces the concurrent collector to keep running. The kernel is doing exactly what it was designed to do; the problem is that *Python's user-facing semantics for `list.append`* (mutate in place) get translated to *persistent-list semantics* (allocate a new spine) at the bytecode level. That mismatch is the issue.

## What protoJS already did

protoJS went through a 7-stage optimisation cycle (P-JS-0..P-JS-7) in May 2026. The cumulative impact was **−62 % wall-clock** on the geomean; the dispatch loop alone (P-JS-7) was the single biggest single-step win. The pattern map below shows which of those wins applies to protoPython.

| protoJS commit | What it did | Applies to protoPython? |
|---|---|---|
| `b989e88a` perf(runBytecode): stack-allocated `externalSlots` | Pass a stack buffer to `proto::ProtoContext` instead of `nullptr`, so the ctor doesn't `new[]` the slot array. | **Partially.** protoPython has the equivalent SBO in `MemoryManager::ContextScope` (64-slot inline buffer). The ctx ctor is still 10 % — so either `automatic_count > 64` for the common case, or the constructor body itself is the cost. Needs investigation (see #1). |
| `P-JS-7` dispatch_table hoisted to function-scope `static const void*[256]` | One-time init + DCLP gate so the 256-entry computed-goto table is built once per process, not 256+210 stores per call. | **YES.** protoPython's `executeBytecodeRange` shows 9-27 % self-time. Same shape, same fix. |
| `P-JS-6` REFRESH_GLOBAL_OBJ() on demand | Move the `globalObj` refresh out of every opcode dispatch and into the 6 opcodes that actually need it. | **YES.** protoPython very likely re-fetches `pyThread`/`env` more often than necessary — the `__tls_get_addr` + `getPyThread` lines combined are 2-4 % across all benches. |
| `P-JS-4` virtual-dispatch short-circuit for plain objects | When the resolved `JSObjectBehavior` is the default, skip the v-table indirection and call `obj->getAttribute` directly. | **Maybe.** protoPython has type-flag caches (`__pyflags__`) but every `getAttribute` still goes through the slow `descriptor`-dispatch path. There's likely a fast path that bypasses descriptor checks for plain objects. |
| `P-JS-1` per-name accessor key cache (thread-local map) | Cache `__get_<name>__` / `__set_<name>__` ProtoString rope builds per name in a thread-local map. | **Different shape** — protoPython doesn't have the `__get_<name>__` sidecar accessor pattern, so this specific cache doesn't apply. But the related observation (TLS-keyed name caches) is generally useful. |
| `P-JS-5` profile-led OP_call argsList builder | Replace `newList() + N×appendLast` with a single `newList(N, items)` to save N+1 cell allocations per call. | **YES.** Worth checking whether protopy's `runUserFunctionCall` does the cheap-allocation form. |
| `SmallSparseList` inline for ≤3 entries | Closure cells and small dicts go from 3-4 cell allocations to 1 inline cell. | **YES.** Already in protoCore as of May 2026 — protopy benefits automatically. Re-verify it is being hit on the Python instance-dict path. |

## Diagnosis and ranked optimisation plan

### #1 — ProtoContext ctor: identify why SBO is not closing the gap **(highest leverage)**

The `MemoryManager::ContextScope` has a `SBO_SLOTS = 64` inline buffer and placement-new on `ctxStorage_`, so the slot array and the ctx struct are both stack-allocated when the function fits. Yet `ProtoContext::ProtoContext` self-time is **10.14 %** of fib(25). Action items, in order:

1. Add a debug counter that records `(automatic_count, fits_in_SBO)` per call and dump after the bench. If most fib calls have `automatic_count > 64`, the fix is simply bumping `SBO_SLOTS` to 256 (protoJS uses 256).
2. If SBO IS engaging, the cost is inside the ctor body. Profile what `ProtoContext::ProtoContext` itself does:
   - Parent-registration with the space's context list (mutex?)
   - GC root setup (sparse-list resize?)
   - Per-thread allocation cache prep (TLS read)
   - Shard cache init for the mutable hot path
   Each of those is amortisable to "do once per call only when needed".

**Expected impact**: if SBO is misfiring on fib, +5-10 % on call-heavy benches just from bumping the buffer; if the ctor body is the cost, probably 5-8 % once the per-call work is trimmed.

### #2 — Dispatch loop: hoist the computed-goto table out of `executeBytecodeRange`

Self-time of the bytecode dispatcher is 9 % on call-heavy and 27 % on attr-lookup. protoJS's P-JS-7 commit shows exactly this fix:

```cpp
const proto::ProtoObject* runBytecode(...) {
    static const void* dispatch_table[256];          // function-scope static
    static std::atomic<bool> dispatch_initialised{false};
    static std::mutex dispatch_init_mtx;
    if (!dispatch_initialised.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> g(dispatch_init_mtx);
        if (!dispatch_initialised.load(std::memory_order_relaxed)) {
            std::fill_n(dispatch_table, 256, &&L_default);
            dispatch_table[OP_LOAD_FAST]   = &&L_load_fast;
            // ... ~80 opcode mappings ...
            dispatch_initialised.store(true, std::memory_order_release);
        }
    }
    // ... goto *dispatch_table[*pc++]; ...
}
```

Address-of-label values are stable per binary load. Steady-state cost is one acquire-load + a predicted-not-taken branch (~2 cycles), instead of ~470 stores per `runBytecode` entry. For fib(25)'s 121 K calls that is ~57 M stores eliminated.

**Expected impact**: −40 % to −60 % on `executeBytecodeRange` self-time, which means **−10 % to −15 % on attr_lookup** and **−4 % to −5 % on call_recursion**. The largest single-step win available.

### #3 — Cache `getPyThread` / TLS lookups on a per-context slot

`__tls_get_addr` is 1.4-2.6 % and `getPyThread` is 0.7-2.5 %. Together that's 2-5 % of every bench. Action: install the per-thread Python state pointer in a `ProtoContext` automatic local slot at thread-attach time (`registerContext`); every `getPyThread(ctx)` becomes a slot read, no TLS.

**Expected impact**: 2-4 % across all benches, free.

### #4 — Make `diagEnabled` a release-time constant

0.5-2.1 % of every bench is spent in `diagEnabled()` evaluating a flag that is always-false in release builds. Wrap it in a macro that the compiler can constant-fold:

```cpp
#if PROTOPY_DEBUG
inline bool diagEnabled() { return s_diagEnabled.load(...); }
#else
constexpr bool diagEnabled() { return false; }
#endif
```

With `constexpr false`, every `if (diagEnabled()) { ... }` branch is dead-code eliminated.

**Expected impact**: 0.5-2 % across all benches, free.

### #5 — Reduce `hasPendingException` rate from per-opcode to per-block

0.9-5.8 % of every bench is spent checking exception state. The current model checks after every opcode. Replace with a generation-counter scheme: a single atomic counter incremented by `raise`/`set_exception`, sampled by the bytecode loop at safepoints (every N opcodes, or at backward-branch / call entry / return).

**Expected impact**: 1-4 % on attr_lookup and similar tight loops.

### #6 — `list_append_loop`: mutable-when-owned heuristic

This is a Python-semantics observation. `list.append(item)` is mutation by the user model, but protopy lowers it to `lst = lst.appendLast(item)` because lists in protoCore are persistent. When the bytecode analysis can prove the list reference is owned (single-use, no aliasing), the interpreter can use protoCore's mutable list path (which does in-place mutation under `setAttributeIfEqual`-style CAS).

This needs a compile-time analysis pass: track which `LOAD_FAST` / `STORE_FAST` slots hold lists that never escape. Big project; gains a one-time analysis cost for a per-iteration saving.

**Expected impact**: probably **−80 % on `list_append_loop`** (close to native `std::vector::push_back` cost), but high implementation cost. Worth scoping after #1-5 are in.

### #7 — `runUserFunctionCall`: single-allocation args list

Mirror of protoJS P-JS-5: replace the `newList() + N × appendLast` build of the args list with `ctx->newList(N, items)`. Saves N+1 cell allocations per Python call.

**Expected impact**: 2-3 % on call-heavy benches.

## Suggested execution order

1. **#4 diagEnabled constant-fold** — 5-minute change, immediately measurable.
2. **#2 dispatch table hoist** — half-day change, largest single win.
3. **#1 ProtoContext investigation** — start with the counter, bump SBO if that explains the cost, otherwise dig.
4. **#3 cached pyThread + #5 hasPendingException rate-limit** — go in together since both move the per-opcode bookkeeping.
5. **#7 single-alloc argsList** — small, included naturally during the call-path audit.
6. **#6 mutable-when-owned** — last, large, deferred until the cheaper wins land.

Cumulative target after #1-5: **−25 to −40 % on every bench** without touching the bytecode semantics. That should pull `attr_lookup`, `list_append_loop`, `str_concat_loop` close to or under CPython on the same machine, matching what protoCpp already shows is achievable at the kernel level.

## Reproducing

```bash
# Bench source
git -C ../protoPython rev-parse HEAD

# Profile
perf record -F 999 -g --call-graph=dwarf -o /tmp/p.data \
    ./build-lto/src/runtime/protopy benchmarks/call_recursion.py
perf report -i /tmp/p.data --stdio --no-children --percent-limit 0.5

# Full ratio table (protoCpp / protopy / CPython on the same machine)
cd ../protoCpp && ./benchmarks/bench.sh
```

## Related

- protoCpp benchmark numbers that motivated this: <https://github.com/gamarino/protoCpp/blob/main/RESULTS.md>
- protoJS P-JS-7 dispatch table hoist (commit `b989e88a` on protoJS) — the canonical reference for #2.
