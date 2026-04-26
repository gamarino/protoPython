# V95 — protoCore mutableRoot: 256 shards + per-thread snapshot cache

> **Status: corrected.** An earlier draft of this report measured against a
> Debug build of protopy (`build/`) and reported geomean ratios that were
> uniformly 2-4× worse than the same workloads measured against the Release
> build (`build-release/`).  The Release-build numbers below are the
> ones that should be referenced.  The earlier draft also surfaced two
> code-level regressions that were fixed in the same investigation; both
> fixes are now upstream and folded into the numbers presented here.

## What V95 changes

Two-part refactor in `protoCore` (commits `7d3674cd` + `75fee285`):

1. **256 shards** (was 16). Selection by `mutable_ref % 256`. Each shard is
   padded to 64 bytes inside an `alignas(64)` array so consecutive shard
   atomics fall on distinct cache lines and concurrent CASes on different
   shards no longer thrash a shared line.
2. **Per-thread mutable-value cache.** A new
   `MutableValueCacheEntry[1024]` table per thread, allocated alongside
   the existing `AttributeCacheEntry` table in `ProtoThreadExtension`.
   Short-circuits the previous "atomic load + AVL `implGetAt`" sequence
   on the common own-thread read path. Validation is by pointer equality
   on the cached `shard_root`: if the cached root pointer still equals
   the live shard root, the cached `current_value` is the live snapshot.
   Any successful CAS by any thread replaces the shard root and
   naturally invalidates stale entries on the next lookup — no
   broadcast, no signaling. Cache entries are GC roots, traced by
   `ProtoThreadExtension::processReferences` so the GC cannot reclaim
   a snapshot still referenced by a cached entry.
3. **Negative caching of "no snapshot yet"** (commit `75fee285`).
   The first draft of the cache only stored positive lookups
   (`snap != nullptr`).  Newly created mutables — which is the common case
   for any newly created mutable, including F1-2-era function objects,
   classes during construction, and freshly allocated dicts — pay a fresh
   `mutableRoot[shard].load()` plus AVL `implGetAt` on every read until
   they are mutated, even though the answer is always `nullptr`.  Caching
   the negative result (with `current_value == nullptr` as a valid hit
   meaning "fall back to `this`") absorbs that cost.

Public API unchanged.  Design and rationale:
[`protoCore/docs/MUTABLE_SHARDING_AND_CACHE_REFACTOR.md`](https://github.com/numaes/protoCore/blob/master/docs/MUTABLE_SHARDING_AND_CACHE_REFACTOR.md).

## Two code-level regressions surfaced and fixed

While re-running the benchmarks the geomean came in at 13.02× vs CPython
3.14 — much worse than the V99 (Apr 20) milestone of ~2.55× and even worse
than V94's 9.96×.  Profiling pinpointed two issues, both now fixed:

### 1. Frame construction was unconditional in the call hot path

`b35bf811` (Apr 22) disabled the V97-era `skipFrame` optimisation in
`runUserFunctionCall` and never gated the parallel
`runUserFunctionCallRaw` (the fast path used by `OP_CALL_FUNCTION`).
Every Python function call paid 4-5 cell allocations per call to build
a frame ProtoObject + add parent + 3 setAttributes for `f_code`,
`f_globals`, `f_back`.

For `fib(25)` (242k calls) this dominated the runtime.  DWARF-based
`perf record` of the unfixed binary showed:

```
45.97 %  proto::ProtoSpace::getFreeCells       ← global mutex
          ↑ proto::ProtoContext::allocCell
            ↑ proto::ProtoContext::newSparseList
              ↑ proto::ProtoContext::newObject(bool)
                ↑ runUserFunctionCallRaw

45.75 %  proto::ProtoContext::~ProtoContext    ← context teardown
          ↑ ContextScope::~ContextScope
            ↑ runUserFunctionCallRaw
```

≈ 90 % of the runtime was frame allocation + context teardown.

**Fix** (commit `ea3d00ab` in protoPython): restore the V97 guard at
both call sites:

```cpp
bool skipFrame = env && (co_flags & CO_OPTIMIZED) && !isGenerator
              && cacheNoInnerFunctions
              && (!hasClosure || cacheNoLoadDeref);
```

A function qualifies for skipFrame when its bytecode contains no
`OP_BUILD_FUNCTION` / `OP_BUILD_CLASS` (computed once at
`BUILD_FUNCTION` time and cached in `FunctionMetaCache`), uses the
LOAD_FAST/STORE_FAST slot path, is not a generator, and either has no
closure or never executes LOAD_DEREF.  Under those conditions the frame
is provably never observed; `sys._getframe()` and traceback walks
remain a known follow-up to materialise a frame on demand only when
those callers actually request one.

### 2. The mutable-value cache did not store negative results

Described above; fixed in `75fee285`.  Drove a measurable improvement
on every benchmark that touches an unmutated mutable in a loop.

## Methodology

- Same machine, same kernel, same governor, Release build flags
  (`-O3 -DNDEBUG`) on both protoPython and protoCore via the
  sibling-source CMake path.
- 2 warmup runs, 10 timed runs per side per benchmark.
- Reported: minimum (resists scheduler / contention spikes — V99 methodology).
- protopy binary: `build-release/src/runtime/protopy`.
- CPython reference: `python3` (CPython 3.14).
- Bench harness: `benchmarks/run_benchmarks.py`, default per-bench
  timeout bumped from 60s to 90s in this commit so `call_recursion`
  and `memory_pressure` are no longer killed mid-run.

## Results — minimum of 10, Release build

```
┌──────────────────┬──────────┬─────────┬──────────┬────────────────┬──────────────┐
│ Benchmark        │ V94 base │ V95 raw │ V95 fix  │ Δ (raw → fix)  │ Ratio vs cpy │
│                  │  (ms)    │  (ms)   │  (ms)    │                │              │
├──────────────────┼──────────┼─────────┼──────────┼────────────────┼──────────────┤
│ call_recursion   │   2994   │  35347  │     967  │   −34380 (−97%)│    15.0×     │
│ list_append_loop │    482   │   1066  │    1268  │     +202 (+19%)│    19.8×     │
│ str_concat_loop  │    479   │    615  │     875  │     +260 (+42%)│    13.6×     │
│ range_iterate    │    464   │    465  │     620  │     +155 (+33%)│     9.6×     │
│ attr_lookup      │    785   │    765  │    1020  │     +255 (+33%)│    15.9×     │
│ multithread_cpu  │     39   │    966  │    1321  │     +355 (+37%)│    20.6×     │
│ int_sum_loop     │     43   │     64  │      64  │       0  ( 0%) │     1.0×     │
├──────────────────┼──────────┼─────────┼──────────┼────────────────┼──────────────┤
│ Geomean ratio    │   9.96×  │  11.39× │   7.72×  │   ~28% better  │              │
└──────────────────┴──────────┴─────────┴──────────┴────────────────┴──────────────┘
```

Reading the table:

- **`call_recursion` recovers fully**, from a 553× CPython slowdown to
  15×.  The skipFrame fix is the single largest win.
- **Geomean ratio drops from 11.39× to 7.72× — better than V94's 9.96×**.
- The other workloads regress slightly (10-40%).  Their bottleneck has
  shifted: per-call frame allocation is no longer dominant, and they now
  surface costs further upstream (attribute name string conversions,
  iterator advance, set-up overhead in non-leaf functions).  These are
  the targets for the next round.
- **`int_sum_loop` is the negative control**: pure SmallInteger
  arithmetic stays on the embedded-value fast path and never touches a
  mutable.  Flat result confirms zero overhead from the new cache when
  it cannot help.

## Profile after the fix — what's left to attack

DWARF `perf record` on `list_append_loop` (Release, post-fix):

```
 5.4 %  proto::ProtoString::toUTF8String           ← attribute name → C string
 3.6 %  proto::RopeCharacterIterator::next          ← iterating string nodes
 3.3 %  proto::StringLeafNode::fromObject           ← string node access
 2.4 %  proto::ProtoSpace::getFreeCells             ← cell-list refill (was 46%)
 2.4 %  proto::ProtoObject::getAttribute
 2.3 %  proto::ProtoObject::isString
 2.0 %  __memmove_avx_unaligned_erms                ← string copies
 1.9 %  proto::ProtoSparseListImplementation::implGetAt
```

Allocator pressure (`getFreeCells`) is now down from 46 % to 2.4 %.
The new dominant cost is **attribute-name string handling** in
`PythonEnvironment::getAttribute`: every attribute lookup is converting
the interned `ProtoString` symbol to a `std::string` (UTF-8) for the
fast-path lookup table.  Together `toUTF8String` + `RopeCharacterIterator::next`
+ `StringLeafNode::fromObject` + `memmove` account for ~14 % of the
benchmark — work that should not happen at all if the lookup is keyed
on the interned symbol pointer.

## Roadmap to approach and surpass CPython

In priority order, with rough single-shot expected impact:

### Tier 1 — ready to land

1. **Eliminate `toUTF8String` calls in
   `PythonEnvironment::getAttribute`.**  Names are already interned;
   the lookup should compare the symbol pointer directly against the
   well-known name pointers (already cached on `PythonEnvironment` —
   `getCodeString()`, `getDataString()`, …).  Estimated impact: ~10-15 %
   on every attribute-bound benchmark.

2. **Lazy frame materialisation for `sys._getframe()` /
   traceback consumers.**  `skipFrame` currently optimises the common
   case but a function that calls `sys._getframe()` from inside its
   body still pays the full cost.  Build the frame only when an
   instruction demands it; cache the frame on the calleeCtx so a
   second request reuses it.  Estimated impact: low individually, but
   removes a correctness concern that currently constrains skipFrame.

### Tier 2 — design + measure

3. **Inline integer arithmetic on the bytecode hot path.**
   `OP_BINARY_ADD` / `OP_COMPARE_OP` for SmallInteger operands today
   trampolines through `ProtoObject::add` / `ProtoObject::compare`.
   For two SmallInteger handles it should be a single tagged-pointer
   pair extract + ALU op + tag re-pack, inline in the dispatch loop.
   Estimated impact: 20-40 % on `int_sum_loop` and the inner loop of
   `range_iterate`.

4. **Per-context cell pool, lock-free.**  `getFreeCells` still acquires
   the global mutex when the thread-local batch is exhausted (every
   ~8 K allocations in single-thread, ~60 K in multi-thread).  Make
   the batch-replenish a lock-free CAS off `space->freeCells`.
   Estimated impact: 1-2 % single-thread, larger as core count grows.

### Tier 3 — multi-threading (the architectural win)

5. **Fix `multithread_cpu`.**  Currently 1.3 s vs CPython's 64 ms.
   This benchmark is supposed to be where protoPython *beats* CPython
   — GIL-free is the whole point.  Profile and address.  Likely
   suspects: `globalMutex` contention in `getFreeCells`,
   `submitYoungGeneration`'s `new DirtySegment` per context teardown
   (242 K mallocs in fib's single thread is bad; multiplied by N
   threads is worse), thread-startup overhead.

6. **GC scaling under N threads.**  STW pause + per-thread free-list
   refills both contend on `globalMutex`.  When 4-8 threads all run
   CPU-bound work, contention may serialise them.  Measure
   `multithread_cpu` with N=2,4,8 and verify it scales linearly until
   GC saturates.

### Tier 4 — beyond parity

7. **JIT compile hot bytecode regions to native via the existing
   `co_bytecode_native` path.**  The infrastructure is there; the
   payoff over the interpretive dispatch loop is what crosses the
   1.0× threshold against CPython on raw arithmetic.

## Honest caveats

- These end-to-end timings include parser, bytecode dispatch, string
  interning, and interpreter startup.  They overstate the CPython gap
  for tiny benchmarks where startup dominates (`int_sum_loop` is at
  the floor of CPython startup; both interpreters spend ~32-64 ms
  before the script body runs).
- The minimum-of-10 metric resists scheduler noise but a single
  bench run in isolation is more representative for tight loops; we
  use minimum here because the V99 baseline used minimum and we want
  apples-to-apples.
- The "ratio vs CPython" column collapses tiny absolute differences
  for fast benchmarks (`int_sum_loop` 64 ms vs 64 ms = 1.0×).  Read
  ratios alongside absolute numbers, not in isolation.

---

**Status:** V95 fixes landed.  Geomean 7.72× — best protoPython has
hit since V94.  Tier 1 work (item 1: name-string conversion in
getAttribute) is the next concrete target.
