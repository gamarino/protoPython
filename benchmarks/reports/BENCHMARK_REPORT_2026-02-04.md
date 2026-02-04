```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      16.42   │      32.82   │ 0.50x faster │
│ int_sum_loop           │       7.89   │      32.34   │ 0.24x faster │
│ list_append_loop       │      16.15   │      32.27   │ 0.50x faster │
│ str_concat_loop        │      16.21   │      32.29   │ 0.50x faster │
│ range_iterate          │      11.24   │      32.31   │ 0.35x faster │
│ multithread_cpu        │      32.44   │      32.84   │ 0.99x faster │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.47x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```