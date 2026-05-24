# protoPython performance audit — 2026-05-24

Platform: Linux x86_64 (AMD Ryzen 5 5500U, 6 physical cores), median of 5 runs.
CPython baseline: 3.14.0 free-threading build (`/usr/local/bin/python3.14`).
protopy: `build_release/src/runtime/protopy` (current HEAD `832d67db`).

Ratios are protoPython-mode / CPython-time: `<1.0` = faster than CPython, `>1.0` = slower.

> ⚠️ **The `protopyc` column in the raw run was noise** — `run_module` does
> not resolve `libprotoPython.so.0` on the current build layout (and when
> `LD_LIBRARY_PATH` is set manually, the loaded `.so` SIGSEGVs). Every
> `protopyc` cell collapsed to ~1135 ms (load-failure time). The AOT
> pipeline needs its own repair pass before it can be benchmarked again;
> the table below excludes the protopyc column.

| Benchmark              | CPython (ms) | protopy (ms) | py/cp        | RSS py/cp        |
|------------------------|-------------:|-------------:|--------------|------------------|
| startup_empty          |       29.93  |       20.17  | 0.67x fast   |  21.0 /  10.8 MB |
| int_sum_loop           |       31.24  |       21.35  | 0.68x fast   |  20.9 /  10.6 MB |
| list_append_loop       |       28.18  |      238.62  | 8.47x slow   |  87.9 /  10.9 MB |
| str_concat_loop        |       28.52  |      266.95  | 9.36x slow   |  71.9 /  10.6 MB |
| range_iterate          |       31.17  |      146.68  | 4.71x slow   |  55.9 /  10.8 MB |
| multithread_cpu        |      571.41  |      797.90  | 1.40x slow   | 630.4 /  10.8 MB |
| attr_lookup            |       36.17  |      191.13  | 5.28x slow   |  53.1 /  10.6 MB |
| call_recursion         |       38.16  |      113.28  | 2.97x slow   |  21.2 /  10.6 MB |
| memory_pressure        |       51.37  |     2779.22  | 54.10x slow  | 1061.2/  10.8 MB |
| pyperf_fib             |      350.49  |     3958.61  | 11.29x slow  |  21.2 /  10.6 MB |
| pyperf_binary_trees    |      146.14  |     6640.85  | 45.44x slow  | 782.4 /  10.8 MB |
| pyperf_nqueens         |      190.37  |     5856.19  | 30.76x slow  | 789.5 /  10.8 MB |
| pyperf_richards_lite   |      136.86  |      615.74  | 4.50x slow   |  54.9 /  10.5 MB |
| pyperf_sieve           |       38.29  |      350.89  | 9.16x slow   | 213.2 /  10.8 MB |
|------------------------|--------------|--------------|--------------|------------------|
| **Geomean (n=14)**     |              |              | **6.23x**    |                  |

## What changed vs. 2026-05-13 (`three-column-final-geomean.md`)

The two snapshots are NOT directly comparable in absolute time — the
CPython baseline shrank ~6× across most tests (179 → 30 ms on
`startup_empty`, 189 → 31 ms on `int_sum_loop`), which means either the
benchmark drivers themselves shrank in workload OR the CPython binary
changed. The *ratios* are the only safe like-for-like comparison:

| Benchmark              | 13-may py/cp | 24-may py/cp | Δ              |
|------------------------|-------------:|-------------:|----------------|
| startup_empty          | 0.65         | 0.67         | ~stable        |
| int_sum_loop           | 0.65         | 0.68         | ~stable        |
| list_append_loop       | 8.31         | 8.47         | ~stable        |
| str_concat_loop        | 10.07        | 9.36         | slight gain    |
| range_iterate          | 3.09         | **4.71**     | **regressed**  |
| multithread_cpu        | **0.07**     | **1.40**     | **see note**   |
| attr_lookup            | 3.30         | **5.28**     | **regressed**  |
| call_recursion         | 1.50         | **2.97**     | **regressed**  |
| memory_pressure        | 33.66        | 54.10        | regressed (excl. from geomean) |
| pyperf_fib             | 9.43         | 11.29        | regressed      |
| pyperf_binary_trees    | 26.60        | **45.44**    | **regressed**  |
| pyperf_nqueens         | 34.00        | 30.76        | slight gain    |
| pyperf_richards_lite   | 5.54         | 4.50         | gain           |
| pyperf_sieve           | 10.84        | 9.16         | gain           |
| **Geomean**            | **4.25**     | **6.23**     | **regressed**  |

**`multithread_cpu` is NOT a protoPython regression**: it reflects that
CPython 3.14 free-threading now runs the 4-thread integer loop fully in
parallel (CPython wall went 2569 → 571 ms, a 4.5× speed-up consistent with
4 threads on 6 cores). protoPython's wall went 174 → 797 ms; the script's
`CHUNK` was tuned up (now 2M iterations × 4 = 8M ops, see the file
header) to make the GIL effect visible on CPython, so the larger absolute
time is expected. The honest message is that the GIL-free advantage no
longer exists on this benchmark vs. CPython 3.14t — both runtimes are now
real parallel, and CPython is faster per thread.

## Priority ranking — what to attack first

Three signals matter: **ratio magnitude**, **whether the test is a real
workload (CPython > 100 ms) vs. startup-dominated (CPython < 50 ms)**, and
**whether the cost is intrinsic or already understood**. Tests where
CPython finishes in 28–38 ms are essentially measuring interpreter startup
plus a tiny workload — they're noisy, and a 5× ratio there is much less
load-bearing than a 5× ratio on a 100+ ms workload.

### Tier S — attack first (real workload, large gap, growing)

| Test | py/cp | CPython ms | Why it matters |
|------|------:|-----------:|----------------|
| **pyperf_binary_trees** | 45.44x | 146.1 | Real workload. Worst ratio among real-workload tests. **Regressed 26.6 → 45.4** in 11 days. Allocation-heavy (tree node construction + GC). 782 MB RSS = ~5× CPython. The combination of *high ratio + active regression + clear cost driver (allocator/GC)* makes this the highest-yield target. |
| **pyperf_nqueens**      | 30.76x | 190.4 | Real workload, list-of-list backtracking. 789 MB RSS. Same cost family as binary_trees but with more pure-interpreter pressure. Fixing what surfaces in binary_trees likely moves this too. |

### Tier A — attack second (real workload, large gap, stable)

| Test | py/cp | CPython ms | Why it matters |
|------|------:|-----------:|----------------|
| **pyperf_fib**          | 11.29x | 350.5 | Pure call/return + tagged-int arithmetic on the longest CPython workload in the suite. Stable ratio. Diagnoses interpreter dispatch + call-frame overhead with almost no allocator noise. Good "control" test to confirm whether binary_trees fixes are allocator-only or also dispatch. |
| **pyperf_richards_lite** | 4.50x | 136.9 | Real workload, lowest real-workload ratio. Improvements here are a good "ceiling" signal. |

### Tier B — defer (startup-dominated, conclusions are weak)

`int_sum_loop`, `list_append_loop`, `str_concat_loop`, `range_iterate`,
`attr_lookup`, `call_recursion`, `pyperf_sieve` all complete on CPython
in 28–38 ms. At that scale the timed signal is dominated by interpreter
startup; the ratios are inflated because the workload is a small fraction
of total wall time. Fixing them in isolation gives misleading geomean
movement. Re-tune their work counts upward (target CPython ≥ 200 ms)
before treating them as serious profiling targets.

### Excluded from priority

* `memory_pressure` — by design protoCore defers GC; the absolute number
  is informative for RSS regressions but the ratio is not a fair fight
  (memory note: not meaningful in comparisons).
* `multithread_cpu` — as explained above, the gap closed because CPython
  3.14 free-threading is real now, not because protoPython regressed.
  No interpreter-level work here unless we want to attack per-thread
  arithmetic dispatch (which `pyperf_fib` already exercises better).
* `startup_empty` — already faster than CPython.

## Recommended first investigation: `pyperf_binary_trees`

**Why first**:
1. Largest ratio (45×) on a real workload.
2. Active regression (+71% on the ratio in 11 days).
3. Largest RSS multiple (5.3× CPython) — points at a concrete cost
   driver (per-node allocation + GC pressure) rather than diffuse
   "interpreter slow" signals.
4. Workload fits comfortably in cgroup-limited memory; reproducible.

**Investigation plan (Phase 1 of systematic debugging)**:

1. `perf record -g` on `protopy benchmarks/pyperf/bench_binary_trees.py`
   under the cgroup; collect a flamegraph. Look for whether time is in
   `Cell` allocation, GC marking, dict / SparseList growth, or
   interpreter dispatch (`ExecutionEngine::dispatch*`).
2. Bisect the regression: re-run the same script against `git show
   <commit>:benchmarks/pyperf/bench_binary_trees.py` for the commit
   closest to 2026-05-13, and rebuild protopy at the 2026-05-13 HEAD.
   Identify whether the regression is in the workload script, the
   interpreter, or the allocator. Memory note
   [project_protocore_attrcache_regression_may2026] already records one
   known attr-cache revert in this window — confirm whether that revert
   was actually applied to the current binary.
3. If allocation-bound: instrument cell-allocation count per test (the
   harness can already capture peak RSS; add an allocations counter via
   `PROTOCORE_GC_STATS=1` if it exists, otherwise add it).
4. Compare ratio of allocations to CPython object creations on the same
   workload — a 10× allocations multiple at 5× RSS multiple suggests
   short-lived churn that the allocator should pool, not heap bloat.

**Single hypothesis to test first**: the May allocator/attr-cache rework
window left `binary_trees`-shaped allocation patterns paying for a
per-allocation cost (cache miss or freelist contention) that the older
code path absorbed. Bisecting the protoCore commits between 2026-05-13
and HEAD on this single benchmark will either confirm or refute it
within an afternoon.

## Suite hygiene follow-ups (separate work)

* **Fix `protopyc` benchmarking path**: `run_module` rpath is stale and
  the current `.so` SIGSEGVs even with `LD_LIBRARY_PATH` set. Either
  rebuild `run_module` against the current `libprotoPython.so.0`, or
  have `run_benchmarks.py` skip the column with an explicit "broken"
  marker instead of silently recording 1135 ms per row.
* **Re-tune startup-dominated micros** (`int_sum_loop`, `list_append_loop`,
  `str_concat_loop`, `range_iterate`, `attr_lookup`, `call_recursion`,
  `pyperf_sieve`) so CPython baseline is ≥ 200 ms; otherwise their
  contribution to the geomean is dominated by startup noise.
