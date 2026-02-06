```
┌─────────────────────────────────────────────────────────────────────┐
│ CPython 3.14 baseline only (protoPython N/A — startup hang)         │
│ (median of 2 runs, N=100000 where applicable)                       │
│ 2026-02-05 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Notes         │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       N/A    │      48.61  │ baseline      │
│ int_sum_loop           │       N/A    │      32.24  │ baseline      │
│ list_append_loop       │       N/A    │      32.34  │ baseline      │
│ str_concat_loop        │       N/A    │      32.28  │ baseline      │
│ range_iterate          │       N/A    │      32.30  │ baseline      │
│ multithread_cpu        │       N/A    │      32.31  │ baseline      │
└─────────────────────────────────────────────────────────────────────┘

Legend: protoPython was not measured (known startup/GC hang; see TESTING.md).
```