```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │      20.25  │      18.95  │ 1.07x slower │
│ int_sum_loop           │      25.67  │      20.58  │ 1.25x slower │
│ list_append_loop       │      26.74  │      19.82  │ 1.35x slower │
│ str_concat_loop        │   12014.35  │      22.19  │ 541.53x slower │
│ range_iterate          │      28.42  │      27.52  │ 1.03x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~3.99x        │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better for protoPython.
```