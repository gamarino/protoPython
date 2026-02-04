```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      15.59  │      17.33  │ 0.90x faster │
│ int_sum_loop           │      32.45  │      21.97  │ 1.48x slower │
│ list_append_loop       │      20.62  │      16.66  │ 1.24x slower │
│ str_concat_loop        │   30031.35  │      20.47  │ 1466.77x slower │
│ range_iterate          │      20.48  │      18.44  │ 1.11x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~4.85x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```