```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      13.83  │      19.12  │ 0.72x faster │
│ int_sum_loop           │      37.01  │      54.33  │ 0.68x faster │
│ list_append_loop       │      19.82  │      18.65  │ 1.06x slower │
│ str_concat_loop        │   15013.05  │      16.98  │ 883.90x slower │
│ range_iterate          │      20.67  │      17.94  │ 1.15x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~3.51x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```