```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-05 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      15.94   │      32.26   │ 0.49x faster │
│ int_sum_loop           │      16.00   │      32.20   │ 0.50x faster │
│ list_append_loop       │      32.20   │      32.46   │ 0.99x faster │
│ str_concat_loop        │      32.18   │      32.36   │ 0.99x faster │
│ range_iterate          │      32.16   │      32.37   │ 0.99x faster │
│ multithread_cpu        │    1768.93   │      32.36   │ 54.66x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~1.54x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```