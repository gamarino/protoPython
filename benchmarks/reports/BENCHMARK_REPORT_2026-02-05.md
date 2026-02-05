```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 2 runs, timeouts excluded)                             │
│ 2026-02-05 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │    TIMEOUT │      32.21   │ timeout      │
│ int_sum_loop           │      32.28   │      32.48   │ 0.99x faster │
│ list_append_loop       │    TIMEOUT │      32.19   │ timeout      │
│ str_concat_loop        │    TIMEOUT │      32.20   │ timeout      │
│ range_iterate          │    TIMEOUT │      32.27   │ timeout      │
│ multithread_cpu        │     966.72   │      32.17   │ 30.05x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~5.46x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.
```