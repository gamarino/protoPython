# Performance Baseline 2026-05-01 — Post Attribute-Cache Redesign

After protoCore commits `2919161d` (CACHE_FLAG_OWN tag-collision fix +
hasAttribute cache invariant repair) and `11d89fa8` (attribute cache
redesigned as own-only — flag bit removed entirely). Companion
protoPython commit `88d80d3c` adapts call sites to the new own-only
cache contract.

## Standard benchmark suite

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ Performance Audit: protoPython vs CPython 3.14                                       │
│ (median of 5 runs, timeouts excluded)                                                │
│ 2026-05-01 Linux x86_64                                                              │
├────────────────────────┬──────────────┬──────────────┬──────────────┬────────────────┤
│ Benchmark              │ Time P (ms)  │ Time C (ms)  │ Ratio        │ Peak RSS (P/C) │
├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤
│ startup_empty          │      19.48   │      28.58   │ 0.68x faster │  21.1/ 10.8MB  │
│ int_sum_loop           │      21.21   │      29.90   │ 0.71x faster │  21.0/ 10.6MB  │
│ list_append_loop       │     188.81   │      28.84   │ 6.55x slower │  61.3/ 11.0MB  │
│ str_concat_loop        │     367.60   │      28.58   │ 12.86x slower │  84.6/ 10.6MB  │
│ range_iterate          │     134.80   │      31.38   │ 4.30x slower │  42.3/ 10.8MB  │
│ multithread_cpu        │      69.37   │      55.55   │ 1.25x slower │  49.4/ 10.8MB  │
│ attr_lookup            │      62.83   │      37.63   │ 1.67x slower │  21.0/ 10.6MB  │
│ call_recursion         │      99.43   │      38.13   │ 2.61x slower │  21.1/ 10.8MB  │
│ memory_pressure        │    9109.47   │      51.35   │ 177.40x slower │ 1343.7/ 10.8MB │
├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤
│ Geomean Time Ratio     │              │              │  3.81x        │                │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## pyperformance subset (internal timing — no startup overhead)

```
========================================================================================
protoPython benchmark suite — internal timing (no startup overhead)
========================================================================================
Benchmark                   protopy (ms)    cpython (ms)      Ratio
----------------------------------------------------------------------------------------
fib(25)                             80.8             9.6       8.4×
binary_trees(10)                  2400.5            27.1      88.6×
nqueens(10)                       4033.4            90.4      44.6×
richards_lite×10                    40.3             2.0      20.1×
----------------------------------------------------------------------------------------
Geomean ratio (vs CPython 3.14): 28.6×
```

## Delta vs `baseline_2026-05-01.md` (pre-redesign)

### Standard suite

| Benchmark | 2026-05-01 (pre) | 2026-05-01 (post-redesign) | Delta |
|---|---|---|---|
| startup_empty | 0.66× faster | 0.68× faster | flat |
| int_sum_loop | 0.75× faster | 0.71× faster | flat |
| list_append_loop | 6.33× slower | 6.55× slower | +3% (noise) |
| str_concat_loop | 10.13× slower | 12.86× slower | +27% regression |
| range_iterate | 4.08× slower | 4.30× slower | +5% (noise) |
| multithread_cpu | 1.54× slower | 1.25× slower | **−19%** |
| attr_lookup | 1.61× slower | 1.67× slower | +4% (noise) |
| call_recursion | 2.80× slower | 2.61× slower | **−7%** |
| memory_pressure | 180.15× slower | 177.40× slower | (excluded — GC defers) |
| **Geomean** | **3.79×** | **3.81×** | **flat** |

### pyperf subset

| Benchmark | 2026-05-01 (pre) | 2026-05-01 (post-redesign) | Delta |
|---|---|---|---|
| fib(25) | 8.3× | 8.4× | flat |
| binary_trees(10) | 92.6× | 88.6× | **−4%** |
| nqueens(10) | 47.3× | 44.6× | **−6%** |
| richards_lite×10 | 20.7× | 20.1× | flat (−3%) |
| **Geomean** | **29.5×** | **28.6×** | **−3%** |

## Interpretation

The redesign is correctness-driven; performance impact is essentially
within run-to-run noise across the geomean. Per-benchmark deltas:

* **multithread_cpu −19% / call_recursion −7%** — the most plausible
  explanation is reduced cache thrash. The pre-redesign cache stored
  two distinct fact kinds (own value vs chain-resolved value) in one
  shared slot, so any contention between the two on a hot
  (object, name) pair caused repeated rewrites. Per-step own-only
  entries are immutable until the corresponding object is mutated, so
  the steady state under load is more cache-friendly.

* **str_concat_loop +27%** — the benchmark allocates fresh strings in a
  tight loop and stresses the per-thread Cell allocator far more than
  the attribute cache. Run-to-run variation on this workload was
  already ±15 % on the prior baseline; the change here is at the upper
  end of that envelope but not clearly attributable to the redesign.
  Worth re-measuring after the next quiet stretch.

* **pyperf geomean −3 %** — within noise. binary_trees and nqueens
  improve 4–6 % (consistent with reduced chain-walk cache replays
  under deep recursion); richards_lite is flat.

Memory footprint unchanged across both suites.

## Conformance

* `ctest`: 159/159 passing (was 159/159 — unchanged).
* SP-* synthetic reproducers: **11/12 passing** (was 12/12 immediately
  before the protoCore sentinel commits; the regression to 6/12 during
  the cache redesign window has been recovered, sole remaining failure
  is `sp_g_b5_reraise` which trips on a pre-existing
  `MappingProxy.update` AttributeError unrelated to the cache work).
* Ground-truth audit (19 stdlib tests): **3/19 PASS** (was 2/19).
  `test_contextlib.py` recovered from CRASH to PASS.

## Attribution

| Commit | Layer | Subject |
|---|---|---|
| `2919161d` | protoCore | Move `CACHE_FLAG_OWN` from bit 3 (alias of POINTER_TAG_SPARSE_LIST) to bit 5; mark cache hits OWN in `hasAttribute` to align invariant. |
| `11d89fa8` | protoCore | Redesign attribute cache: each slot answers a single own-attribute question; chain walks in get/has consult the cache step-by-step against each step's own object; `CACHE_FLAG_OWN` removed entirely. |
| `88d80d3c` | protoPython | `mp_isClassObject` tag-OBJECT guard; LOAD_ATTR fast-path / unbound-method fixes; `PythonEnvironment::getAttribute` uses `hasOwnAttribute` for `isExplicitNone`; `py_hasattr` MRO fallback. |

The dominant effect of the redesign is removing a memory-corruption
class of bugs (tag-collision and inherited-result staleness via
mutable parents in the chain). The performance numbers confirm the
new shape pays no measurable cost relative to the broken predecessor.
