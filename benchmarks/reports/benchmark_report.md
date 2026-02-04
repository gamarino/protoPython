```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs, timeouts excluded)                             │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       8.79   │      32.26   │ 0.27x faster │
│ int_sum_loop           │       8.02   │      32.32   │ 0.25x faster │
│ list_append_loop       │      16.16   │      32.36   │ 0.50x faster │
│ str_concat_loop        │      16.19   │      32.43   │ 0.50x faster │
│ range_iterate          │      16.03   │      65.70   │ 0.24x faster │
│ multithread_cpu        │     115.19   │      32.76   │ 3.52x slower │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ Geometric mean         │              │              │ ~0.49x       │
└─────────────────────────────────────────────────────────────────────┘

Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.

multithread_cpu: single-thread comparison (both interpreters run with SINGLE_THREAD=1); ratio reflects per-chunk interpreter speed, not multi-CPU use.
```