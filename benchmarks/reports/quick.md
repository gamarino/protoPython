```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      33.22  │      16.59  │ 2.00x slower │
│ int_sum_loop           │      29.19  │      17.14  │ 1.70x slower │
│ list_append_loop       │      28.64  │      16.59  │ 1.73x slower │
│ str_concat_loop        │   15016.51  │      18.32  │ 819.71x slower │
│ range_iterate          │      34.81  │      19.11  │ 1.82x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~6.15x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```