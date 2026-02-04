```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      14.39  │      15.63  │ 0.92x faster │
│ int_sum_loop           │      21.08  │      17.22  │ 1.22x slower │
│ list_append_loop       │      21.11  │      17.27  │ 1.22x slower │
│ str_concat_loop        │   20021.11  │      18.06  │ 1108.88x slower │
│ range_iterate          │      20.12  │      18.50  │ 1.09x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~4.41x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```