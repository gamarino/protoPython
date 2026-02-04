```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       7.91   │      32.41   │ 0.24x faster │
│ int_sum_loop           │       8.08   │      64.82   │ 0.12x faster │
│ list_append_loop       │      16.35   │      32.66   │ 0.50x faster │
│ str_concat_loop        │      16.22   │      32.35   │ 0.50x faster │
│ range_iterate          │       7.93   │      32.84   │ 0.24x faster │
│ multithread_cpu        │     164.83   │      32.28   │ 5.11x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.46x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```