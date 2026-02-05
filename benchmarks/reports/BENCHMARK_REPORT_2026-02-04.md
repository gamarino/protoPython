```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       7.92   │      32.49   │ 0.24x faster │
│ int_sum_loop           │      16.26   │      32.65   │ 0.50x faster │
│ list_append_loop       │      33.37   │      33.62   │ 0.99x faster │
│ str_concat_loop        │      16.30   │      32.22   │ 0.51x faster │
│ range_iterate          │      16.06   │      33.82   │ 0.47x faster │
│ multithread_cpu        │     114.68   │      65.38   │ 1.75x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.61x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```