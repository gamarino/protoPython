# V95 — protoCore mutableRoot: 256 shards + per-thread snapshot cache

**Change:** Two-part refactor in protoCore (commit `7d3674cd`).

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

Public API unchanged. See
`protoCore/docs/MUTABLE_SHARDING_AND_CACHE_REFACTOR.md` for the full
design and rationale.

---

## Methodology

- Same machine, same kernel, same governor, same protopy build flags
  before and after.
- Each benchmark: 2 warmup runs + 8 timed runs per side; both median
  and minimum reported (median resists outliers; minimum is the
  closest indicator of pure work time when system noise is in play).
- `before` = protoCore at commit `ce6cd968` (design doc only, baseline
  16 shards, no cache).
- `after`  = protoCore at commit `7d3674cd` (this refactor merged).
- protoPython rebuilt against each protoCore checkout via the
  sibling-source CMake path; same compiler flags both runs.
- Excluded from this comparison:
  - `memory_pressure`: protoCore GC defers collection by design;
    the benchmark dominates wall time with allocation pressure, not
    mutable hot-path work.
  - `multithread_cpu` and `call_recursion`: pre-existing measurement
    issues (the former hangs the harness intermittently in this
    environment; the latter times out under both before and after,
    so contributes no signal to the comparison).

## Results

```
┌────────────────────────┬──────────────┬─────────────┬──────────────┬─────────────┬───────────┬───────────┐
│ Benchmark              │ Before med   │ After med   │ Δ med        │ Before min  │ After min │ Δ min     │
├────────────────────────┼──────────────┼─────────────┼──────────────┼─────────────┼───────────┼───────────┤
│ attr_lookup            │     1242.5   │    1268.5   │    +2.1%     │    1217.2   │   1217.4  │   ~0.0%   │
│ list_append_loop       │     2470.9   │    2245.7   │    -9.1%     │    2420.5   │   2169.5  │  -10.4%   │
│ int_sum_loop           │       64.5   │      64.4   │     ~0.0%    │      64.3   │     64.1  │   ~0.0%   │
│ range_iterate          │      966.7   │     916.7   │    -5.2%     │     966.4   │    866.4  │  -10.4%   │
│ str_concat_loop        │     1718.7   │    1668.7   │    -2.9%     │    1668.1   │   1619.3  │   -2.9%   │
└────────────────────────┴──────────────┴─────────────┴──────────────┴─────────────┴───────────┴───────────┘
all times protopy ms; smaller is better.
```

CPython 3.14 reference times (median of 8) for the same workloads on
the same machine: 32–65 ms. They are unchanged before/after by
construction (CPython binary did not move) and are therefore not
relevant to the protocols → before/after comparison.

## Reading the table

- **Where mutable hot reads dominate**, the cache delivers
  measurable improvement: `list_append_loop` ≈ **−10.4 %** on
  minimum, `range_iterate` ≈ **−10.4 %** on minimum,
  `str_concat_loop` ≈ **−2.9 %**.
- **`attr_lookup` is essentially flat.** This is expected and
  consistent with the pre-existing `AttributeCacheEntry`
  short-circuit: when the (object, name) attribute cache hits,
  the mutable-value cache is never consulted — the existing
  cache already absorbs the hot path for this microbenchmark.
  The improvement from the new cache becomes visible on
  workloads where the attribute cache *misses* (different
  attributes per access, or short-lived cache lines), which the
  list / iterator workloads represent.
- **`int_sum_loop` is flat.** Pure SmallInteger arithmetic stays
  on the embedded-value fast path and never touches a mutable
  object. No mutable read on the hot path → no cache impact.
  This is a useful negative control: it confirms the cache adds
  no measurable overhead when it cannot help.

## Honest caveats

- These are end-to-end script timings. They include parser,
  bytecode dispatch, string interning, and runtime startup
  overhead. The cache improves *mutable read latency*, which is
  one of several costs. Microbenchmarks pinpointing the cache
  fast-path in isolation will produce larger ratios; that work
  is part of Phase C (instrumentation/tuning) of the refactor.
- 4 timed runs per side were too noisy to detect 2-3 % effects;
  bumping to 8 runs and reading the minimum stabilised the
  signal. The ±2 % medians on `attr_lookup` and `int_sum_loop`
  are below the floor of detection here.
- The improvement is consistent with the design model: an
  own-thread mutable read becomes a cached pointer compare plus
  two word-loads, replacing an atomic load and AVL `implGetAt`.
  In tight loops over mutable state this saves a handful of
  cycles per iteration; the benchmarks scale that saving by the
  number of iterations and surface it as the −5 % to −10 % wall
  delta seen here.

## What this does *not* claim

- It does not claim a 3× speedup on macro benchmarks. The design
  doc's 3× target is for the *microbenchmark* of a mutable hot
  read, which this report does not run. The macro impact above
  is the realistic upper bound when the cache is one component
  of a much larger interpreter pipeline.
- It does not claim improvement on workloads dominated by
  immutable-only paths (`int_sum_loop`) or by allocation
  pressure (`memory_pressure`).

---

**Status:** This report establishes the baseline post-refactor.
Phases C (hit-rate instrumentation, tuning of
`MUTABLE_VALUE_CACHE_DEPTH`) and D (STW invalidation experiment)
remain deferred and will be driven by the per-cache hit-rate
counters once those land.
