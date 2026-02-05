```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-05 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      33.84   │      37.52   │ 0.90x faster │
│ int_sum_loop           │      32.32   │      64.63   │ 0.50x faster │
│ list_append_loop       │      32.45   │      36.03   │ 0.90x faster │
│ str_concat_loop        │      64.50   │      32.50   │ 1.98x slower │
│ range_iterate          │      34.60   │      64.68   │ 0.53x faster │
│ multithread_cpu        │    2335.08   │      32.40   │ 72.08x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~1.77x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```