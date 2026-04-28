# Performance analysis — why protoPython is 113.6× slower than CPython on real Python code

**Date:** 2026-04-28
**Baseline:** [`benchmarks/reports/baseline_2026-04-28.md`](../benchmarks/reports/baseline_2026-04-28.md)
**Method:** `perf record --call-graph=dwarf -F 999` against `bench_richards_lite.py` (the worst of the three pyperformance subsets at 204.5×).
**Build:** `build-release` (Release, `-O2 -DNDEBUG`), protoCore 1.1.0.

---

## TL;DR

The 113.6× geomean gap on the pyperformance pure-Python subset comes from
four cost centres, in roughly equal share, all on every Python function
call and every attribute read.  None of them is interpreter dispatch
itself (only 4.5% of CPU); the dispatch loop is fine.  The cost is in
what each opcode calls into.

| Cost centre                                           | % CPU | What                                                      |
|-------------------------------------------------------|------:|-----------------------------------------------------------|
| Attribute resolution (multiple sites)                 | ~46%  | `getAttribute` + AVL `implGetAt` + `hasOwnAttribute` + ProtoPython wrapper + interning + memcmp + `isObject` calls inside them |
| Per-call frame lifecycle (`ProtoContext` ctor + dtor) | ~22%  | `~ProtoContext` 11.24% + `getFreeCells` 11.08% (refill triggered by destructor returning cells) |
| `proto::isObject` virtual-getType call                |  9.3% | Tag check that routes through a virtual `Cell::getType()` PLT call |
| Bytecode dispatcher itself                            |  4.5% | `executeBytecodeRange` — actually fine; not the bottleneck |
| Other interpreter glue                                |  ~7%  | `runUserFunctionCallRaw`, `getType`, `getInternedString`, `RecursionScope` ctor, etc. |

---

## Top hot functions (perf, no children, 614 samples)

```
14.52%  proto::ProtoObject::getAttribute
11.24%  proto::ProtoContext::~ProtoContext
11.19%  proto::ProtoSparseListImplementation::implGetAt
11.08%  proto::ProtoSpace::getFreeCells
 9.29%  proto::isObject
 4.54%  protoPython::executeBytecodeRange
 3.52%  proto::ProtoObject::hasOwnAttribute
 2.85%  __memcmp_avx2_movbe
 1.99%  protoPython::PythonEnvironment::getAttribute
 1.59%  protoPython::PythonEnvironment::getInternedString
 1.50%  protoPython::PythonEnvironment::getType
 1.34%  proto::ProtoSparseListImplementation::implHas
 1.18%  proto::ProtoObject::isString
 1.03%  protoPython::runUserFunctionCallRaw
```

---

## Cause #1 — attribute resolution dominates (~46% of CPU)

`richards_lite` does method dispatch in a tight loop: `worker.run(value)`
4000 times per round, 200 rounds.  Each call walks the prototype chain:

1. `worker` → look up `run` → not own → walk parent (Worker class) → found
2. Bind it as a method → unwrap and invoke

Every step calls `getAttribute` or `hasOwnAttribute`, which:

- Iterates the prototype chain (linearised parent list).
- At each level, does an AVL `implGetAt` on the attribute SparseList
  (`ProtoSparseListImplementation::implGetAt` — 11.19%).
- Calls `isObject(currentValue)` to check for object/non-object dispatch
  (the 9.3% virtual-getType cost flows through here).
- Falls through `PythonEnvironment::getAttribute` (1.99%) for the
  Python-specific attribute lookup chain.

There **is** a per-thread attribute cache
(`AttributeCacheEntry[THREAD_CACHE_DEPTH]` in
`ProtoThreadExtension`), keyed by `(currentValue, name)`.  Looking at
the assembly of `getAttribute`, the cache lookup path executes early
and is presumably hitting on the steady state.  But the function still
costs 14.5% of CPU — the cache *check* itself (hash, two pointer
compares, plus the prologue) plus the cases where `currentValue`
changed (mutable objects after a write moved them to a fresh
snapshot — common for instance attributes that mutate during run).

Concrete numbers for richards_lite:
  - `Worker.run` is on the *class* prototype, not the instance.
    Looking up `run` on a `worker` instance always misses the
    instance's own attributes and walks to the class — at least
    one prototype-chain step per dispatch.
  - `self.next` and `self.counter` are instance attributes that
    mutate during the run; their slots churn through the mutable
    cache instead of stabilising.
  - 800K dispatches × ~5μs per resolution dominates the 40ms
    benchmark.

## Cause #2 — per-call frame lifecycle (~22% of CPU)

Every Python function call constructs a `ProtoContext` for the call
frame and destroys it on return.

```
11.24%  ~ProtoContext  — return cells, submit young generation,
                         build ReturnReference, all under globalMutex
11.08%  getFreeCells   — refill the per-context freeCells batch
                         from the global pool, also under globalMutex
```

These two are *the same lock* on the same global free list, hit twice
per call (once at the destructor returning unused cells, once at the
next caller's getFreeCells refill).  V154's frame-skip optimisation
(`skipFrame` for CO_OPTIMIZED leaf calls) cut this for *some* call
sites; it does not apply to method dispatch (the richards_lite case)
because the bound-method path does not currently take the fast frame
skip.

Allocator pressure dropped from 46% to 11% over V92-V154 (per the
README notes), but at 11% it's still the second-largest cost centre on
real benchmarks.

## Cause #3 — `proto::isObject` is not a tag check (~9.3% of CPU)

```cpp
// core/LargeInteger.cpp
bool isObject(const ProtoObject* obj) {
    if (!obj) return false;
    ProtoObjectPointer pa{}; pa.oid = obj;
    if (pa.op.pointer_tag != POINTER_TAG_OBJECT) return false;
    return toImpl<const Cell>(obj)->getType() == CellType::Object;  // ← virtual
}
```

The reader's mental model is "this is a tag-bit check, should be one
or two cycles".  Reality:

1. nullptr check and tag-bit extraction: cheap.
2. **Virtual call to `Cell::getType()`**: misses across the protoCore
   shared-library DSO boundary (PLT trampoline), is unpredictable
   (Cell has many subclasses), can't be inlined.

Every `getAttribute` call site does several `isObject` checks.  Times
4000 dispatches/round × 200 rounds × multiple checks per dispatch =
many millions of virtual calls to `getType()`.

## Cause #4 — interpreter dispatch is fine (~4.5%)

`executeBytecodeRange` at 4.54% says the bytecode dispatcher itself is
not where the time goes.  What each opcode calls into is.  This is the
opposite shape from a JIT-target — there's nothing to micro-optimise
in the dispatch table; the wins come from cheaper opcodes.

---

## Proposed improvements, in priority order

### Tier A — high impact, modest effort

#### A1 · Inline `isObject` to a real tag-bit check  (target: 9.3% → <1%)

The virtual `getType()` call is unnecessary.  The pointer tag
(POINTER_TAG_OBJECT) plus a single non-virtual byte field on `Cell`
exposing a `CellTypeRaw` enum is enough:

```cpp
// header (inlinable, no DSO crossing)
inline bool isProtoObjectCell(const ProtoObject* obj) {
    if (!obj) return false;
    ProtoObjectPointer pa{}; pa.oid = obj;
    if (pa.op.pointer_tag != POINTER_TAG_OBJECT) return false;
    return reinterpret_cast<const Cell*>(obj)->cellTypeRaw_ == CELL_OBJECT;
}
```

`cellTypeRaw_` would be a `uint8_t` in the `Cell` header, set in each
subclass constructor.  Existing virtual `getType()` stays for
non-hot-path callers that want polymorphism.  Replace `isObject` body
to call the new inline.

Expected impact: collapses the 9.3% to a ~0.5% memory load.

#### A2 · Extend `skipFrame` to method-call dispatch  (target: 22% → ~5%)

V154's `skipFrame` already detects "leaf function with no inner
allocations" and reuses the parent context.  Extend the detection to
the bound-method dispatch path used by `worker.run(...)` — the call
goes through `runUserFunctionCallRaw`, currently always builds a
fresh `ProtoContext`.

The `Worker.run` body in richards_lite calls only `self.next.run(...)`
which is itself a leaf-or-recursive frame; nothing forces a separate
context.  Lifting skipFrame eligibility to the bound-method path would
recover most of the per-call overhead.

#### A3 · Per-thread cell pool, no globalMutex on hot path  (target: cut globalMutex contention)

Today `~ProtoContext` returns unused cells to `space->freeCells` under
`globalMutex`, and the next `getFreeCells` re-acquires the same lock
to refill.  Replace this round-trip with a per-thread pool:

- Each thread keeps a thread-local `freeCellsLocal` list.
- `getFreeCells` first pops from local (no lock).
- `~ProtoContext` pushes returned cells onto local (no lock).
- Spill local → global only when local exceeds N cells (e.g. 4096),
  and refill from global only when local is empty.

Expected impact: drops both `~ProtoContext` and `getFreeCells` to
near-free on the steady state; only thread startup / GC pause hits
the global pool.

### Tier B — medium impact, more invasive

#### B1 · Inline cache for `getAttribute` keyed by `(name, parent_chain)`

protoCore already has a thread-local `AttributeCacheEntry` table
keyed by `(currentValue, name)`.  But for method dispatch, the
*looked-up object changes* (it's the result of walking up to the
class, not the original receiver) so the cache hit rate on the chain
walk is below 100%.

A complementary cache keyed by `(receiver_class, name) → (level, value)`
would let `worker.run(...)` short-circuit the walk: same class, same
attribute name → cached level + cached attribute pointer.  Validity
check: a single `mutableRoot[shard].root.load() == cached_root`
comparison.

Expected impact: cut `getAttribute` from 14.52% to ~3-4% on
method-heavy benchmarks.

#### B2 · `LOAD_METHOD` / `CALL_METHOD` opcodes (skip bound-method allocation)

CPython 3.7+ pairs `LOAD_METHOD` + `CALL_METHOD` to skip materialising
the bound-method object on every call.  protoPython currently always
materialises the bound method (which itself is a Cell allocation
under the GC).

Implement `LOAD_METHOD`/`CALL_METHOD` in the compiler and dispatcher.
On the call path, the bound-method allocation goes away; on the
attribute side, `LOAD_METHOD` can use a more focused cache than
`LOAD_ATTR` (the cache only needs to resolve the function pointer +
"is method?" flag, not the full bound-method object).

Expected impact: another 10-15% on method-heavy benchmarks (richards,
nqueens).

#### B3 · "Small-object" attribute storage (flat array, not AVL)

`ProtoSparseListImplementation::implGetAt` is 11.19% of CPU.  AVL
trees are correct but have ~3× the per-lookup cost of a linear search
on small tables.  Most Python objects have <16 attributes; the
hot path `Worker` instance has 3 (`ident`, `counter`, `next`).

Add a `ProtoSmallAttributes` representation: flat
`pair<key, value>[N]` up to N=12, promoted to AVL on overflow.  Same
public API.

Expected impact: cut `implGetAt` from 11.19% to ~3-4%.

### Tier C — research / longer-term

#### C1 · Hidden-classes / shape transitions (V8-style)

Same `Worker` class instantiated 20 times has the same "shape".
Cache the attribute layout per shape; method-dispatch lookups become
direct offset reads.  Big lift (compiler changes + cache plumbing)
but pays the most on real OO code — the richards_lite + nqueens
gap is exactly this pattern.

#### C2 · Tracing JIT for hot bytecode regions

Tier C from the existing roadmap.  Complementary to all of A and B:
once per-opcode cost is collapsed, the dispatch loop itself becomes
the next bottleneck and a JIT crosses the 1.0× threshold on
richards/nqueens.

---

## Estimated cumulative impact of Tier A + Tier B

|              | Now (richards_lite) | After Tier A | After Tier A+B |
|--------------|--------------------:|-------------:|---------------:|
| Ratio vs CPython |             204.5× |       ~80×  |          ~25× |
| Geomean (3 bench)|             113.6× |       ~50×  |          ~15× |

Numbers are rough — each tier opens up follow-on wins as the dominant
cost centres rotate.  The first 12× drop from 1337× → 113.6× came from
a single change (mutable-cache routing); A1 + A2 + A3 should produce a
similar magnitude of improvement, this time from cell-pool + frame-
skip + tag-check work.

## Out of scope for this analysis

- `memory_pressure` (247× slower).  Known anomaly:
  protoCore's GC defers collection by design; not a pyperformance
  category and not a fix target.
- protoJS-side improvements.  protoCore changes (A1, A3) will benefit
  protoJS too at no extra cost.
- Multi-threaded scaling.  `multithread_cpu` is already at 1.56×;
  it's not the bottleneck on the pyperf subset.
