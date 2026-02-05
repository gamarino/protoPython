# Performance Suite Analysis — 2026-02-05

**Report:** [BENCHMARK_REPORT_2026-02-05.md](BENCHMARK_REPORT_2026-02-05.md)  
**Harness:** `PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --output reports/BENCHMARK_REPORT_2026-02-05.md`  
**Environment:** Linux x86_64, median of 5 runs, 60s timeout per run.

---

## Summary

| Benchmark         | protopy (ms) | cpython (ms) | Ratio     | Note                    |
|-------------------|-------------:|-------------:|----------:|-------------------------|
| startup_empty     |        32.28 |        32.20 | 1.00×     | On par                  |
| int_sum_loop      |        32.26 |        32.93 | 0.98×     | Slightly faster         |
| list_append_loop  |        35.21 |        33.03 | 1.07×     | Slightly slower         |
| str_concat_loop   |        32.19 |        32.39 | 0.99×     | Slightly faster         |
| range_iterate     |        32.42 |        32.61 | 0.99×     | Slightly faster         |
| multithread_cpu   |       218.25 |        64.65 | **3.38×** | Slower; shared allocator|

**Geometric mean (ratio):** ~1.23× (multithread_cpu is the main gap).

---

## Per-benchmark analysis

### Where protoPython is on par or faster

- **startup_empty (1.00×):** Importing `abc` is on par with CPython.
- **int_sum_loop (0.98×):** Integer loop and `sum(range(N))` are slightly faster; protoCore numeric path is efficient.
- **list_append_loop (1.07×):** List append loop is close; minor variance.
- **str_concat_loop (0.99×):** String concatenation in a loop is on par.
- **range_iterate (0.99×):** Iteration over `range(N)` is on par.

### Where protoPython is slower

- **multithread_cpu (3.38×):** protoPython ~218 ms vs CPython ~65 ms. CPython runs the 4 chunks under the GIL (effectively serial). protoPython is GIL-less; threads run in parallel but still contend on the **shared allocator** (`getFreeCells` lock when a thread exhausts its batch). Lock-free `submitYoungGeneration` and per-thread resolve cache / thread-local trace have removed the previous hot-path mutexes (see [MULTITHREAD_CPU_BENCHMARK.md](../MULTITHREAD_CPU_BENCHMARK.md)). **Recommendation:** To get protoPython faster than CPython on this workload, per-thread heaps (LocalHeap in protoCore) are needed; see [REARCHITECTURE_PROTOCORE.md](../../docs/REARCHITECTURE_PROTOCORE.md).

---

## Conclusions

1. **Single-threaded:** protoPython is on par with CPython across startup, int sum, list append, string concat, and range iteration (ratios ~0.98×–1.07×).
2. **Multithreaded CPU:** Ratio improved from historical ~70× to **~3.38×** after allocator and lock-free fixes; remaining gap is the shared allocator. Geometric mean is ~1.23×.

---

## How to reproduce

```bash
cd /path/to/protoPython
PROTOPY_BIN=$(pwd)/build/src/runtime/protopy python3 benchmarks/run_benchmarks.py \
  --output reports/BENCHMARK_REPORT_2026-02-05.md --timeout 60
```

Optional: `-v` for per-run timings, `-q` for quick (2 runs, 1 warmup).
