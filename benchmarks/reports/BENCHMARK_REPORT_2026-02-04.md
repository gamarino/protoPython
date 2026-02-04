```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       8.41   │      32.37   │ 0.26x faster │
│ int_sum_loop           │      16.19   │      32.33   │ 0.50x faster │
│ list_append_loop       │      16.07   │      32.34   │ 0.50x faster │
│ str_concat_loop        │      16.07   │      32.33   │ 0.50x faster │
│ range_iterate          │      16.32   │      33.27   │ 0.49x faster │
│ multithread_cpu        │     816.90   │      32.92   │ 24.82x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.86x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```