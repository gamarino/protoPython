```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       5.61  │      16.14  │ 0.35x faster │
│ int_sum_loop           │       5.65  │      29.73  │ 0.19x faster │
│ list_append_loop       │       5.67  │      16.31  │ 0.35x faster │
│ str_concat_loop        │   30032.00  │      20.22  │ 1485.22x slower │
│ range_iterate          │       5.74  │      18.48  │ 0.31x faster │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~1.60x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```