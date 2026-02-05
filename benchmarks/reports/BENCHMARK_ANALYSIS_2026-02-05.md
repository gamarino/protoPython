# Performance Suite Analysis — 2026-02-05

**Report:** [benchmark_report.md](benchmark_report.md)  
**Harness:** `PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --output reports/benchmark_report.md`  
**Environment:** Linux x86_64, median of 5 runs, 90s timeout per run.

---

## Summary

| Benchmark         | protopy (ms) | cpython (ms) | Ratio     | Note                    |
|-------------------|-------------:|-------------:|----------:|-------------------------|
| startup_empty    |        33.84 |        37.52 | 0.90×     | Slightly faster         |
| int_sum_loop     |        32.32 |        64.63 | 0.50×     | ~2× faster              |
| list_append_loop |        32.45 |        36.03 | 0.90×     | Slightly faster         |
| str_concat_loop  |        64.50 |        32.50 | **1.98×** | Slower; string path     |
| range_iterate    |        34.60 |        64.68 | 0.53×     | ~2× faster              |
| multithread_cpu  |      2335.08 |        32.40 | **72.08×**| Slower; threading model |

**Geometric mean (ratio):** ~1.77× (dominated by multithread_cpu and str_concat_loop).

---

## Per-benchmark analysis

### Where protoPython is faster or on par

- **startup_empty (0.90×):** Importing `abc` is slightly faster; less interpreter startup overhead.
- **int_sum_loop (0.50×):** Integer loop and `sum(range(N))` benefit from protoCore numeric path; ~2× faster than CPython.
- **list_append_loop (0.90×):** List append loop is on par; allocation and list ops are efficient.
- **range_iterate (0.53×):** Iteration over `range(N)` is ~2× faster; aligns with int_sum_loop.

### Where protoPython is slower

- **str_concat_loop (1.98×):** String concatenation in a loop is ~2× slower. Likely causes: repeated UTF8/ProtoString conversion, temporary allocations, or less optimized string buffer path. **Recommendation:** Profile and optimize the string concat path (e.g. pre-size or use a list-join pattern in the benchmark; or improve runtime string builder).
- **multithread_cpu (72.08×):** protoPython ~2335 ms vs CPython ~32 ms. CPython runs the 4 chunks under the GIL (effectively single-threaded). protoPython is GIL-less but current execution still **serializes** compile+run per thread (see `TestExecutionEngine.ConcurrentExecutionTwoThreads` and [GIL_FREE_AUDIT.md](../../docs/GIL_FREE_AUDIT.md)): one shared `PythonEnvironment`/context and a mutex around allocation and execution. So we do not yet get parallel CPU-bound speedup; instead we pay thread and serialization overhead. **Recommendation:** This is the target of the re-architecture ([REARCHITECTURE_PROTOCORE.md](../../docs/REARCHITECTURE_PROTOCORE.md)): work-stealing scheduler, per-thread LocalHeap, and task batching so that multithreaded CPU workloads can run in parallel without mutex contention.

---

## Conclusions

1. **Single-threaded numeric and iteration:** protoPython is competitive or faster (startup, int sum, list append, range iterate).
2. **String-heavy loop:** protoPython is ~2× slower; worth a dedicated optimization pass.
3. **Multithreaded CPU:** Current design does not yet show a parallelism win; geometric mean and worst ratio are dominated by this benchmark. Improving it requires the protoCore/protoPython re-architecture (scheduler, heaps, no mutex in hot path).

---

## How to reproduce

```bash
cd /path/to/protoPython
PROTOPY_BIN=$(pwd)/build/src/runtime/protopy python3 benchmarks/run_benchmarks.py \
  --output reports/benchmark_report.md --timeout 90
```

Optional: `-v` for per-run timings, `-q` for quick (2 runs, 1 warmup).
