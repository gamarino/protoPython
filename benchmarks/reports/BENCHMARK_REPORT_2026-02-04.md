# Benchmark Report: protoPython vs CPython 3.14

**Date:** 2026-02-04  
**Platform:** Linux x86_64  
**Run:** CPython baseline only (protoPython skipped — see Status below).

---

## Results (median of 5 runs)

```
┌─────────────────────────────────────────────────────────────────────┐
│ Performance: protoPython vs CPython 3.14                            │
│ (median of 5 runs; N=100000 where applicable)                       │
│ 2026-02-04 Linux x86_64                                              │
├─────────────────────────────────────────────────────────────────────┤
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio/Notes   │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ startup_empty          │       N/A    │      17.44   │ baseline     │
│ int_sum_loop           │       N/A    │      24.68   │ baseline     │
│ list_append_loop       │       N/A    │      18.41   │ baseline     │
│ str_concat_loop        │       N/A    │      26.07   │ baseline     │
│ range_iterate          │       N/A    │      24.31   │ baseline     │
│ multithread_cpu        │       N/A    │      24.23   │ baseline     │
└─────────────────────────────────────────────────────────────────────┘

Legend: protoPython was not measured (runtime crash — see Status). Ratio = protopy/cpython when both run.
```

---

## Status: protoPython run

During this run, **protoPython (protopy) was not measured** because the executable aborted with **stack smashing detected** when running `--module abc` or script benchmarks. This is a runtime bug (e.g. buffer overflow) to be fixed; it is not a hang. Once fixed, re-run:

```bash
PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --output reports/benchmark_report.md
```

to obtain full protoPython vs CPython comparison and ratios.

---

## Analysis (CPython baseline)

- **startup_empty (17.44 ms):** Time to import `abc`; useful baseline for interpreter startup.
- **int_sum_loop (24.68 ms):** `sum(range(100_000))`; CPU-bound, single-thread.
- **list_append_loop (18.41 ms):** 10k appends; exercises list growth.
- **str_concat_loop (26.07 ms):** String concatenation loop; often slower than list append in CPython.
- **range_iterate (24.31 ms):** Iteration over `range(100_000)`; tight loop.
- **multithread_cpu (24.23 ms):** Four threads each doing `sum(range(50_000))`; total work = 4 × chunk. On CPython the GIL serializes the threads, so wall time is similar to a single-thread run of the same total work (~24 ms). When protoPython is measured, running the **same total work in a single thread** (no GIL, no thread overhead) can show **dramatically less** wall time if protoPython’s per-chunk cost is comparable and thread overhead is avoided. See [MULTITHREAD_CPU_BENCHMARK.md](../MULTITHREAD_CPU_BENCHMARK.md).

**Conclusion:** CPython baselines are consistent (roughly 17–26 ms per benchmark on this machine). The multithread_cpu benchmark is intended to highlight GIL limitation on CPython and the benefit of a GIL-less runtime once protoPython runs correctly.

---

## How to re-run after fixing protoPython

```bash
cd /path/to/protoPython
PROTOPY_BIN=./build/src/runtime/protopy CPYTHON_BIN=python3 python3 benchmarks/run_benchmarks.py --timeout 45 --output reports/benchmark_report.md
```

Then check the generated report for ratios (protopy/cpython); for multithread_cpu, ratio &lt; 1 means protoPython was faster (dramatically less time).
