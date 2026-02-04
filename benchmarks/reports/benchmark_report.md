```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       7.97   │      32.25   │ 0.25x faster │
│ int_sum_loop           │      16.35   │      35.77   │ 0.46x faster │
│ list_append_loop       │      16.04   │      32.28   │ 0.50x faster │
│ str_concat_loop        │      16.05   │      32.27   │ 0.50x faster │
│ range_iterate          │      16.45   │      32.29   │ 0.51x faster │
│ multithread_cpu        │     115.46   │      33.00   │ 3.50x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.61x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```