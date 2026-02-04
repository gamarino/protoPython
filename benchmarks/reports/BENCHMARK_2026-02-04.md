```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      16.81   │      32.77   │ 0.51x faster │
│ int_sum_loop           │      19.31   │      64.91   │ 0.30x faster │
│ list_append_loop       │      16.22   │      32.41   │ 0.50x faster │
│ str_concat_loop        │       8.08   │      32.32   │ 0.25x faster │
│ range_iterate          │      15.23   │     121.87   │ 0.12x faster │
│ multithread_cpu        │     116.86   │      66.46   │ 1.76x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.40x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```