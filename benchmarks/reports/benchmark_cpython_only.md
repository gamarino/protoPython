```
┌─────────────────────────────────────────────────────────────────────┐
│ CPython 3.14 baseline only (protoPython N/A — startup hang)         │
│ (median of 5 runs, N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Notes         │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       N/A    │      17.44  │ baseline      │
│ int_sum_loop           │       N/A    │      24.68  │ baseline      │
│ list_append_loop       │       N/A    │      18.41  │ baseline      │
│ str_concat_loop        │       N/A    │      26.07  │ baseline      │
│ range_iterate          │       N/A    │      24.31  │ baseline      │
│ multithread_cpu        │       N/A    │      24.23  │ baseline      │
└─────────────────────────────────────────────────────────────────────┘

Legend: protoPython was not measured (known startup/GC hang; see TESTING.md).
```