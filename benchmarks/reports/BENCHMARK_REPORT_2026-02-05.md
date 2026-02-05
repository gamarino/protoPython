```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-05 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      32.28   │      32.20   │ 1.00x slower │
│ int_sum_loop           │      32.26   │      32.93   │ 0.98x faster │
│ list_append_loop       │      35.21   │      33.03   │ 1.07x slower │
│ str_concat_loop        │      32.19   │      32.39   │ 0.99x faster │
│ range_iterate          │      32.42   │      32.61   │ 0.99x faster │
│ multithread_cpu        │     218.25   │      64.65   │ 3.38x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~1.23x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```