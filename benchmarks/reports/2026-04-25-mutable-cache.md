# V154 — mutable cache + frame-skip + attribute-name fast path + multithread fix

> **Status: V154.** This file consolidates five rounds of work landed
> in April 2026:
>   1. **V95** — protoCore mutableRoot: 256 shards + per-thread snapshot cache.
>   2. **V95-fix** — two regressions surfaced and fixed: build-mode
>      methodology (Debug vs Release) and unconditional frame
>      construction in `runUserFunctionCallRaw`.
>   3. **V154 (Tier 1.1)** — `PythonEnvironment::getAttribute` keys-fallback
>      no longer allocates `std::string` per comparison; uses
>      `ProtoString::cmp_to_string` directly.
>   4. **V154 (Tier 3 round 1)** — `_thread.start_joinable_thread` +
>      `_ThreadHandle` actually spawn real OS threads (the previous
>      stub returned 0 and `threading.Thread.start()` hung forever);
>      a thread-startup race in protoCore was closed and a public
>      `ProtoContext::safepoint()` API added so the bytecode
>      dispatcher participates in GC stop-the-world from
>      allocation-free hot loops.
>   5. **V154 (Tier 2)** — inline SmallInt fast path in
>      `OP_BINARY_ADD` / `OP_INPLACE_ADD` / `OP_BINARY_SUBTRACT` /
>      `OP_INPLACE_SUBTRACT` / `OP_COMPARE_OP`.  Four new `static
>      inline` helpers in `protoCore.h`
>      (`isSmallInt`, `asSmallInt`, `smallIntInRange`, `makeSmallInt`)
>      let the opcode handler branch on the tag and do the arithmetic
>      inline, skipping ~10 cross-DSO function calls and 6 redundant
>      tag checks for the >90 % SmallInt+SmallInt case.
>
> All numbers below are Release build, minimum of 10 runs (V99
> methodology).  The protoPython project version is **1.0.0**;
> "V154" refers to the internal performance-milestone ordinal that
> tracks each iteration of the optimisation roadmap.

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

V94 baseline → V95 raw → V95 fixed → V154 Tier 1.1 + Tier 3 round 1 →
V154 + Tier 2 → V154 + Tier 2.5 (dispatcher polish + multithread workload rewritten).

```
┌──────────────────┬──────────┬─────────┬──────────┬──────────┬──────────┬───────────┬──────────────┐
│ Benchmark        │ V94 base │ V95 raw │ V95 fix  │ V154 T3  │ V154 T2  │ V154 T2.5 │ Ratio vs cpy │
│                  │  (ms)    │  (ms)   │  (ms)    │  (ms)    │  (ms)    │  (ms)     │  (V154 T2.5) │
├──────────────────┼──────────┼─────────┼──────────┼──────────┼──────────┼───────────┼──────────────┤
│ call_recursion   │   2994   │  35347  │     967  │     866  │     716  │     515   │    7.98×     │
│ list_append_loop │    482   │   1066  │    1268  │    1067  │    1017  │    1017   │   15.82×     │
│ str_concat_loop  │    479   │    615  │     875  │     716  │     716  │     716   │   11.13×     │
│ range_iterate    │    464   │    465  │     620  │     615  │     565  │     465   │    7.23×     │
│ attr_lookup      │    785   │    765  │    1020  │     917  │     967  │     816   │   12.68×     │
│ multithread_cpu  │     39†  │    966  │    1321  │     165  │     164  │     215‡  │    3.33×     │
│ int_sum_loop     │     43   │     64  │      64  │      32  │      32  │      32   │    1.00×     │
├──────────────────┼──────────┼─────────┼──────────┼──────────┼──────────┼───────────┼──────────────┤
│ Geomean ratio    │   9.96×  │  11.39× │   7.72×  │   5.53×  │   5.34×  │   5.06×   │              │
└──────────────────┴──────────┴─────────┴──────────┴──────────┴──────────┴───────────┴──────────────┘
```

‡ `multithread_cpu` workload rewritten in T2.5: CHUNK 5 000 →
**50 000**, single-function tight integer-accumulator loop (no nested
call frames).  10× more work per worker; wall time only grew 165 ms →
215 ms, confirming the prior 165 ms was largely spawn/join overhead
masking the real parallel-CPU number.  Honest ratio is now 3.33×.

† `multithread_cpu` baselines through V95-fix were measured against a
benchmark that silently fell back to **sequential execution** when
`threading._has_thread` evaluated false — i.e. they were not
multi-threaded measurements at all and made protoPython look
unrealistically good.  V154 rewrote the benchmark to use `_thread`
directly with a strict assertion that all 4 workers actually publish
their result, which both exposed the latent bugs (a stub
`_thread.start_joinable_thread` and a missing GC safepoint poll) and
yielded the first honest multi-thread number we have on this
codebase.  165 ms is **real** four-thread work.

Reading the table:

- **Tier 2.5 — call_recursion 716 → 515 ms (-28 %).**  After Tier 2,
  a fresh DWARF profile of `call_recursion` showed `get_env_diag()`
  PLT stubs (5.30 % CPU) and `hasPendingException()` via getAttribute
  (3.79 % CPU) inside the dispatch loop.  Two surgical fixes:
  hoist the diag flag to a local at function entry (61 PLT calls
  per loop iteration → zero), and replace the pending-exception
  check with a TLS bool mirror so the inline `hasPendingException()`
  collapses to a single TLS read.  Combined: `call_recursion`
  ratio drops 13.46× → **7.98×** vs CPython.  `range_iterate`
  -18 %, `attr_lookup` -16 %.  Geomean 5.34× → **5.06×** (best ever).
- **Tier 2 (SmallInt fast path) lands the latest deltas:**
  `call_recursion` 866 ms → **716 ms** (-17 %; fib's recurrence is the
  ideal case — every call does one COMPARE_OP and three integer
  arithmetic ops on SmallInt operands), `range_iterate` 615 ms →
  **565 ms** (-8 %), `str_concat_loop` flat (the inner loop is
  dominated by string concat, not SmallInt arithmetic),
  `multithread_cpu` flat (already at the thread setup-overhead floor;
  arithmetic is no longer the bottleneck).  **Geomean drops from
  5.53× to 5.34× — best protoPython has hit.**

- **`multithread_cpu` was the prior headline**: 1217 ms → **165 ms**, ratio
  18.90× → 2.55×.  Threading was structurally broken
  (`Thread.start()` hung; the legacy benchmark's `_has_thread = False`
  branch hid this for V92-V99).  After the fix it really runs four OS
  threads in parallel; the remaining 2.55× gap is `globalMutex`
  contention in `getFreeCells` and per-context `new DirtySegment`
  mallocs, both Tier 3 follow-ups.
- **`call_recursion` keeps improving**: V95 raw 35 347 ms → V154 866
  ms, a 41× cumulative speed-up.  The skipFrame fix was the single
  largest contributor; Tier 1.1 (`cmp_to_string`) and the safepoint
  poll added the last few percent.
- **Geomean ratio at 5.53×** — best protoPython has hit.  V94 milestone
  was 9.96×; V95-fix was 7.72×; this is a 24 % relative drop on top of
  V95-fix, dominated by the multi-thread fix.
- **Tier 1.1 (`cmp_to_string`) lands −5 to −15 % across attribute-bound
  benchmarks**: list_append_loop −15.9 %, str_concat_loop −12.4 %,
  call_recursion −5.3 %.  Negligible on benchmarks that don't trigger
  the keys-fallback path.
- **`int_sum_loop` is the negative control**: pure SmallInteger
  arithmetic stays on the embedded-value fast path and never touches a
  mutable.  Flat result confirms zero overhead from the changes when
  they cannot help, including the new safepoint poll
  (one relaxed atomic load per 64 opcodes is below the noise floor here).

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

## Pyperformance pure-Python subset (the realistic baseline)

> Added 2026-04-26 in response to the observation that the
> microbenchmark suite above only exercises favourable workloads
> (tight integer loops on the SmallInt fast path) and dramatically
> understates the gap to CPython on real Python code.  This subset is
> 3 small benchmarks ported from the official PSF
> [pyperformance](https://github.com/python/pyperformance) suite —
> the self-contained, no-external-deps subset — chosen because they
> stress the operations real code spends time on: attribute access,
> method dispatch, list subscript.

Run via `python3 benchmarks/pyperf/run_pyperf_subset.py <protopy>`.
Each script reports its own min-of-5 timing using
`time.perf_counter()` (excludes interpreter startup):

```
┌────────────────────┬──────────────┬──────────────┬──────────┬──────────────────────────────┐
│ Benchmark          │ protoPy (ms) │ CPython (ms) │ Ratio    │ Stresses                     │
├────────────────────┼──────────────┼──────────────┼──────────┼──────────────────────────────┤
│ nqueens(8)         │       2204   │         4.9  │   449×   │ recursion + list[i] subscr   │
│ sieve(10000)       │        933   │         1.0  │   933×   │ list mutate in tight loop    │
│ richards_lite      │       1481   │         0.2  │  7404×   │ class instantiation, methods │
├────────────────────┼──────────────┼──────────────┼──────────┼──────────────────────────────┤
│ Geomean ratio      │              │              │ 1459×    │                              │
└────────────────────┴──────────────┴──────────────┴──────────┴──────────────────────────────┘
```

Compare with the microbenchmark geomean of 5.06× — a 3-orders-of-magnitude
divergence.  The microbenchmarks were faithfully measuring the
SmallInt arithmetic / opcode-dispatch path, but that path is **a
small fraction of real Python execution time**.  Real code is dominated
by:
- **Attribute access** (`obj.attr`): every method call does at least one
  full attribute resolution, walking the MRO and the descriptor protocol.
- **Method dispatch**: bound-method materialisation per call.
- **List/dict subscript**: every `lst[i]` indexes a mutable container
  through the full attribute / `__data__` lookup path.
- **Class instantiation**: each `Worker(i, counter)` allocates plus runs
  `__init__` plus walks MRO for both lookups.

That is what closes the gap to CPython, not faster integer arithmetic.

A few earlier attempts to port other pyperformance scripts surfaced
correctness or performance pathologies that need attention:

| Bench attempted | Outcome | Diagnosis |
|---|---|---|
| `nqueens` with closure (`count = [0]; def solve(): count[0] += 1`) | Wrong result (0 instead of 92) | Closure-over-list-mutation issue; rewrote without nested function |
| `fannkuch` n≥6 | Timed out (>60s) | The carry-style permutation loop with `perm1[i+1]` access hits a slow path; under investigation |

These are bugs / opportunities, not closed items.

## Roadmap to approach and surpass CPython

In priority order, with rough single-shot expected impact:

### Tier 1 — landed in V154

1. **Eliminated `toUTF8String` calls in
   `PythonEnvironment::getAttribute` keys fallback.** ✅ Landed.
   Five hot fallback sites that previously did
   `keyS->toUTF8String(ctx, ks); match = (ks == nameStr)` now use
   the zero-allocation `keyS->cmp_to_string(ctx, name) == 0`.
   Measured impact: −5 to −15 % on attribute-bound benchmarks
   (list_append_loop −15.9 %, str_concat_loop −12.4 %).

2. **Lazy frame materialisation for `sys._getframe()` /
   traceback consumers.** ⚙ Deferred (design phase).  Requires a
   "frame factory" attached to the calleeCtx so `sys._getframe(0)`
   can materialise on demand; touches the
   `PythonEnvironment::setCurrentFrame` chain and exception traceback
   code.  Estimated impact: low single-digit; the bigger win is
   removing a latent correctness gap (`sys._getframe()` from inside a
   skipFrame function currently sees the parent's frame).

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

5. **Fix `multithread_cpu` — round 1.** ✅ Landed.  Three fixes:
   - `_thread.start_joinable_thread` was a stub returning 0 with no OS
     thread spawned, so `threading.Thread.start()` hung indefinitely.
     Replaced with a real implementation backed by
     `ProtoSpace::newThread`, plus a `_ThreadHandle` Python-visible
     object exposing `is_done` / `join` / `_set_done`.
   - Thread-startup race in `ProtoThreadImplementation`:
     `runningThreads` was incremented before `std::thread` was
     spawned, leaving GC waiting for a phantom running thread.  Now
     incremented from `thread_main` (the moment the OS thread really
     starts).
   - Pure-bytecode loops never participated in stop-the-world.  Added
     public `ProtoContext::safepoint()` to protoCore (additive, no
     inlining, no library merge) and call it every 64 opcodes from
     protoPython's bytecode dispatcher.
   The benchmark itself was rewritten to use `_thread` directly with a
   strict assertion that all 4 workers actually finish, so any future
   regression to a fake-threading fallback fails the benchmark instead
   of silently passing.  Result: 1217 ms → 165 ms (ratio 18.90× →
   2.55×).

6. **Fix `multithread_cpu` — round 2: beat CPython.** Round 1 made
   the benchmark real and reliable; round 2 is closing the remaining
   2.55× gap.

   An earlier draft of this report (and the previous README) said the
   suspects were `globalMutex` contention in `getFreeCells` and
   per-context `new DirtySegment` mallocs.  **A DWARF profile of the
   working benchmark refutes both.**  Per-thread refill batches are
   `min(blocksPerAllocation * runningThreads * 4, 65 536)` when more
   than one thread is running, so with 4 workers + GC each batch is
   capped at **65 536 cells**.  The benchmark allocates well under
   that, so each worker hits `getFreeCells` exactly once and the
   mutex is perfectly amortised.  Profile shares (Release, minimum-of-10
   run, 165 ms wall):

   ```
   13.47 %  executeBytecodeRange      ← interpreter dispatch loop itself
    6.48 %  ProtoObject::getAttribute
    3.14 %  ProtoSpace::getFreeCells   (≈ 4 calls × ~1.3 ms each; amortised)
    2.81 %  __tls_get_addr             ← thread-local storage lookups
    2.71 %  proto::isObject            ← type-tag checks
    1.76 %  diagEnabled                ← env-var check still in the loop
    1.51 %  GCStack::push_back
    1.19 %  __strlen_avx2
    1.14 %  getPyThread
    1.08 %  ProtoObject::compare
   ```

   The real targets to close the 2.55× gap are the **per-bytecode
   interpreter costs** (the dispatch loop body, getAttribute path,
   TLS lookups, residual diagEnabled PLT stubs in release builds),
   plus collapsing remaining `__tls_get_addr` calls in the hot path.
   Inline SmallInteger arithmetic (Tier 2) is the largest single
   expected win because both arms of the inner loop —
   `s += i` and `i += 1` — go through `ProtoObject::add`'s trampoline
   today.

7. **GC scaling under N threads.**  STW pause + per-thread free-list
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

**Status:** V154 + Tier 2.5 landed.  **Two distinct geomeans depending
on what we measure:**

  - Microbenchmarks (this file's main table): **5.06×** vs CPython —
    best protoPython has hit on the favourable workloads (tight
    integer loops on the SmallInt fast path).
  - Pyperformance pure-Python subset (added today): **1459×** vs
    CPython — real Python code dominated by attribute access, method
    dispatch, and list subscript.

The 3-orders-of-magnitude divergence is the honest picture.  The V92
through V154 work successfully closed the gap on the path the
microbenchmarks exercised (10× → 5× geomean on those), but the
realistic gap is still where it was, because the optimisation surface
shifted but never landed on the dominant cost in real code.

Next concrete targets, re-prioritised in light of the realistic numbers:

1. **Hot-path attribute access** — every `obj.attr` walks the full
   MRO + descriptor protocol.  In richards_lite this is the entire
   1481 ms (CPython does it in 200 µs).  Inline cache for the
   common (instance, attribute_name) → resolved-value pair would be
   the single largest win on real code.
2. **List subscript fast path** — `lst[i]` for SmallInt indices on a
   `list` instance currently goes through getAttribute(__data__) +
   ProtoList::getAt + null-check + boxing.  An inlined fast path in
   `OP_BINARY_SUBSCR` would close most of the nqueens / sieve gap.
3. **Method dispatch fast path** — bound-method materialisation per
   call; pre-resolve the descriptor once at LOAD_METHOD time.
4. Tier 3 round 2 (close the remaining 3.33× gap on
   `multithread_cpu`).
5. Tier 1.2 (lazy frame materialisation for `sys._getframe()`).
6. Computed-goto / threaded dispatch in `executeBytecodeRange`
   (the structural item to break through 5× on call-heavy code, but
   the realistic-bench items above are bigger now).
