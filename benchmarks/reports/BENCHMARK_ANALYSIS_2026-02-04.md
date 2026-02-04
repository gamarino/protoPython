# Benchmark Analysis: protoPython vs CPython 3.14 (2026-02-04)

## Objective

Run the full benchmark suite (protopy and CPython), record results, analyze performance ratios, and document findings.

## Methodology

- **Harness**: `benchmarks/run_benchmarks.py`
- **Reference**: CPython 3.14 (default `python3` or `CPYTHON_BIN`)
- **Runs**: Median of 5 runs after 2 warm-up runs per benchmark
- **Timeout**: 30 s per run
- **Command**: `PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --timeout 30 --output reports/benchmark_report.md`

## Results Summary (this run)

| Benchmark         | protopy (ms) | cpython (ms) | Ratio   | Notes |
|-------------------|--------------|--------------|---------|--------|
| startup_empty     | 5.61         | 16.14        | 0.35x   | protopy faster |
| int_sum_loop      | 5.65         | 29.73        | 0.19x   | protopy faster |
| list_append_loop  | 5.67         | 16.31        | 0.35x   | protopy faster |
| str_concat_loop   | 30032.00     | 20.22        | —       | **Invalid**: timeout(s) |
| range_iterate     | 5.74         | 18.48        | 0.31x   | protopy faster |

Raw report: [benchmark_report.md](benchmark_report.md).

## Analysis

### Benchmarks that complete (no timeout)

- **startup_empty**: protopy ~0.35x (faster). Minimal import path is quick; startup is stable after protoCore GC fixes.
- **int_sum_loop**, **list_append_loop**, **range_iterate**: protopy reports 0.19x–0.35x (faster) because **script bodies are not yet executed** by protopy; only the module shell is loaded and resolved. So these timings reflect environment and module setup only, not the actual loops. CPython runs the full script (e.g. sum over range, list append, range iteration), so cpython times are higher. Once protopy executes bytecode for these loops, ratios will shift.

### str_concat_loop and timeouts

- The reported protopy time (30032 ms) equals the 30 s run timeout. At least one of the 7 runs (2 warmup + 5 measured) hit the timeout, so the **median is invalid** and the geometric mean in the report is skewed.
- See [PERFORMANCE_HANG_DIAGNOSIS.md](../../docs/PERFORMANCE_HANG_DIAGNOSIS.md) for causes and fixes (getFreeCells deadlock fix, moduleRootsMutex, larger OS allocation chunk). Rebuild protoCore/protopy after those fixes and re-run; str_concat_loop may then complete within timeout.
- For a **clean comparison** when str_concat_loop times out: exclude it from the geometric mean or run with `--cpython-only` for a CPython baseline.

### Geometric mean

- The reported geometric mean (~1.60x) is **not representative** because it includes the invalid str_concat_loop ratio. Excluding str_concat_loop, the geometric mean of the four completing benchmarks (0.35, 0.19, 0.35, 0.31) is about **0.29x** (protopy faster on these workloads, with the caveat that script bodies are not executed by protopy).

## Conclusions

1. **Startup and module-load benchmarks**: protopy completes quickly and reports faster than CPython for startup and script load; script bodies are not executed yet.
2. **str_concat_loop**: Can still hit the run timeout; treat median as invalid when it equals or nears the timeout. Ensure protoCore is built with the performance fixes (see PERFORMANCE_HANG_DIAGNOSIS.md).
3. **Reproducibility**: Re-run with the same timeout and report path; for baseline-only, use `--cpython-only`.

## How to re-run

```bash
# Full comparison (protopy vs CPython)
PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --timeout 30 --output reports/benchmark_report.md

# CPython baseline only (no protopy)
python3 benchmarks/run_benchmarks.py --cpython-only --output reports/YYYY-MM-DD_cpython314.md
```
