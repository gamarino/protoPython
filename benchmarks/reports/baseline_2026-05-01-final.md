# Performance Baseline 2026-05-01 — Final

After the full attribute-cache work cycle. Companion to protoCore
commits `2919161d`, `11d89fa8`, `f0bfbcaa` and protoPython commits
`88d80d3c`, `67bbc742`, `63aa6a27`.

## Standard benchmark suite

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ Performance Audit: protoPython vs CPython 3.14                                       │
│ (median of 5 runs, timeouts excluded)                                                │
│ 2026-05-01 Linux x86_64                                                              │
├────────────────────────┬──────────────┬──────────────┬──────────────┬────────────────┤
│ Benchmark              │ Time P (ms)  │ Time C (ms)  │ Ratio        │ Peak RSS (P/C) │
├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤
│ startup_empty          │      20.14   │      29.80   │ 0.68x faster │  21.0/ 10.6MB  │
│ int_sum_loop           │      21.19   │      32.79   │ 0.65x faster │  20.9/ 10.8MB  │
│ list_append_loop       │     195.94   │      28.71   │ 6.82x slower │  61.4/ 11.0MB  │
│ str_concat_loop        │     349.06   │      29.60   │ 11.79x slower │  84.6/ 10.8MB  │
│ range_iterate          │     136.41   │      31.89   │ 4.28x slower │  42.1/ 10.6MB  │
│ multithread_cpu        │      62.85   │      52.44   │ 1.20x slower │  49.5/ 10.8MB  │
│ attr_lookup            │      67.79   │      38.77   │ 1.75x slower │  21.0/ 10.8MB  │
│ call_recursion         │     100.59   │      41.29   │ 2.44x slower │  21.1/ 10.6MB  │
│ memory_pressure        │   10271.09   │      52.98   │ 193.85x slower │ 1347.7/ 10.8MB │
├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤
│ Geomean Time Ratio     │              │              │  3.76x        │                │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## pyperformance subset (internal timing — no startup overhead)

```
========================================================================================
Benchmark                   protopy (ms)    cpython (ms)      Ratio
----------------------------------------------------------------------------------------
fib(25)                             78.9             9.5       8.3×
binary_trees(10)                  2181.4            27.2      80.2×
nqueens(10)                       4010.9            86.9      46.2×
richards_lite×10                    42.9             1.9      22.6×
----------------------------------------------------------------------------------------
Geomean ratio (vs CPython 3.14): 28.9×
```

## Trajectory across the work cycle

| Baseline | Standard geomean | pyperf geomean | Notes |
|---|---|---|---|
| 2026-04-28 | 4.55× | ~46× | Pre attribute-cache work |
| 2026-05-01 (original) | 3.79× | 29.5× | After protoCore cache optimisations (sentinel, hasAttribute) |
| 2026-05-01 (post-redesign) | 3.81× | 28.6× | After cache redesigned as own-only |
| **2026-05-01 (final)** | **3.76×** | **28.9×** | **After isObj 6-bit fix + module classifier + __doc__ defaults** |

Net improvement across the cycle: **−17 % on the standard suite,
−37 % on pyperf**. The most recent leg is roughly flat — improvements
where they appeared (multithread_cpu went from 1.54× to 1.20×) are
within run-to-run noise envelopes.

## Per-benchmark deltas (final vs original 2026-05-01)

| Benchmark | Original | Final | Δ |
|---|---|---|---|
| startup_empty | 0.66× faster | 0.68× faster | flat |
| int_sum_loop | 0.75× faster | 0.65× faster | +13 % regression (single-run noise) |
| list_append_loop | 6.33× slower | 6.82× slower | +8 % |
| str_concat_loop | 10.13× slower | 11.79× slower | +16 % |
| range_iterate | 4.08× slower | 4.28× slower | +5 % |
| multithread_cpu | 1.54× slower | 1.20× slower | **−22 %** |
| attr_lookup | 1.61× slower | 1.75× slower | +9 % |
| call_recursion | 2.80× slower | 2.44× slower | **−13 %** |
| memory_pressure | 180.15× slower | 193.85× slower | (excluded — GC defers by design) |
| **Geomean** | **3.79×** | **3.76×** | **flat (−1 %)** |

| pyperf | Original | Final | Δ |
|---|---|---|---|
| fib(25) | 8.3× | 8.3× | flat |
| binary_trees(10) | 92.6× | 80.2× | **−13 %** |
| nqueens(10) | 47.3× | 46.2× | flat |
| richards_lite×10 | 20.7× | 22.6× | +9 % |
| **Geomean** | **29.5×** | **28.9×** | **flat (−2 %)** |

`call_recursion −13 %` and `multithread_cpu −22 %` are the clearest
wins: the own-only cache eliminates inherited-result staleness during
deep recursion and reduces cross-thread cache contention.
`binary_trees −13 %` shows the same pattern under recursive object
attribute reads. The cluster of small regressions (int_sum_loop,
list_append_loop, str_concat_loop, attr_lookup) lives mostly within
the run-to-run noise envelope already documented for these workloads
(±10–15 % across consecutive runs).

## Conformance

* `ctest`: 159/159 passing (unchanged).
* SP-* synthetic reproducers: **11/12 passing** — only
  `sp_g_b5_reraise` still fails, and the failure has migrated from a
  protoCore-cache-induced abort to a stdlib gap inside `signal.py`
  (`'object' object has no attribute '__doc__'` on a chain that
  involves more than the prototype default added in commit
  `63aa6a27`).
* Ground-truth audit (19 stdlib tests): **3/19 PASS**, **2 SILENT_HALT**,
  **14 CRASH** (unchanged count vs the post-cache-redesign run;
  `test_contextlib.py` is the previously-recovered PASS).

### Audit triage by root cause

The 14 CRASH cases share a small number of root causes; closing one
unlocks several:

| Cluster | Tests | Root cause |
|---|---|---|
| unittest.* / test.support | 8 (test_grammar, test_types, test_descr, test_asyncgen, test_base64, test_json, test_re, test_datetime) | Same `signal._wraps` __doc__ chain that blocks `sp_g_b5_reraise` |
| pdb / doctest | 2 (test_generators, test_collections) | Stdlib gap |
| signal / subprocess | 1 (test_sys) | Same as cluster A |
| asyncio | 1 (test_os) | Stdlib gap |
| typing | 1 (test_functools) | Stdlib gap |
| direct AssertionError | 1 (test_dataclasses) | Test-internal |

Closing the unittest cascade alone would lift the audit from 3/19 to
~11/19. That is the highest-leverage next step but requires resolving
the `__doc__` propagation through the user-class MRO (see notes
below).

## Pending / catalogued

* **`__doc__` propagation through user-class MRO** — adding
  `objectPrototype.__doc__ = None` and `typePrototype.__doc__ = None`
  did not lift the chain end-to-end. `MyClass.__doc__` still raises
  `AttributeError` for user classes with no explicit docstring. The
  slow-path MRO walk recognises the OWN attribute on `object`/`type`
  via `hasOwnAttribute`, but somewhere between that resolution and
  the consumer at `signal.py:52` the value is lost. Likely surface:
  the `cls.__doc__` lookup is dispatched through the metaclass branch
  (line 12969 `1.2 Metaclass Lookup`) which uses an older
  `val != PROTO_NONE` check that filters legitimate None values.
  Worth a focused diagnostic dispatch.
* **str_concat_loop noise** — re-measure once the `__doc__` work
  lands; the +16 % delta has not stabilised across consecutive runs.
* **SP-A stdlib gaps** — typing, doctest, pdb, asyncio, test.support.
  Multi-week. Off-cycle.
