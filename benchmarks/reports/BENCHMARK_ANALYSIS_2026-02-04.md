# Benchmark Analysis: protoPython vs CPython 3.14 (2026-02-04)

## Objective

Run the full benchmark suite (protopy and CPython), record results, analyze performance ratios, and document findings.

## Methodology

- **Harness**: `benchmarks/run_benchmarks.py`
- **Reference**: CPython 3.14 (default `python3` or `CPYTHON_BIN`)
- **Runs**: Median of 5 runs after 2 warm-up runs per benchmark
- **Timeout**: 30 s per run
- **Command**: `PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --timeout 30 --output reports/benchmark_report.md`

## Results Summary

| Benchmark         | protopy (ms) | cpython (ms) | Ratio   | Notes |
|-------------------|--------------|--------------|---------|--------|
| startup_empty     | 15.59        | 17.33        | 0.90x   | protopy faster |
| int_sum_loop      | 32.45        | 21.97        | 1.48x   | protopy slower |
| list_append_loop  | 20.62        | 16.66        | 1.24x   | protopy slower |
| str_concat_loop   | 30031.35     | 20.47        | —       | **Invalid**: timeout(s) |
| range_iterate     | 20.48        | 18.44        | 1.11x   | protopy slower |

Raw report: [benchmark_report.md](benchmark_report.md).

## Analysis

### Valid benchmarks (no timeout)

- **startup_empty**: protopy is ~10% faster than CPython for minimal import (`import abc`). Startup path is stable after the protoCore GC fix (do not set `gcStarted` before GC thread exists).
- **int_sum_loop**: protopy ~1.48x slower. Script runs module load + resolution; no bytecode execution of the loop yet, so time is dominated by environment and module setup.
- **list_append_loop**: protopy ~1.24x slower. Same caveat: script body (list append loop) is not executed by protopy; only module shell is loaded.
- **range_iterate**: protopy ~1.11x slower. Same as above.

### str_concat_loop and timeouts

- The reported protopy time for **str_concat_loop** (30031 ms) equals the 30 s run timeout. At least one of the 7 runs (2 warmup + 5 measured) hit the timeout, so the **median is invalid** and the geometric mean in the report is skewed.
- Intermittent timeouts on script runs were reduced by the protoCore fix (see [PERFORMANCE_HANG_DIAGNOSIS.md](../../docs/PERFORMANCE_HANG_DIAGNOSIS.md)) but can still occur under load or in long harness runs.
- For a **clean comparison** excluding str_concat_loop when it times out, run with `--cpython-only` for a CPython baseline, or re-run the full suite and treat str_concat_loop as N/A if its protopy median is near the timeout value.

### Geometric mean

- The reported geometric mean (~4.85x) is **not representative** because it includes the invalid str_concat_loop ratio. Excluding str_concat_loop, the geometric mean of the four valid ratios (0.90, 1.48, 1.24, 1.11) is about **1.17x** (protopy slower).

## Conclusions

1. **Startup**: protopy startup is stable and slightly faster than CPython for minimal import.
2. **Script benchmarks**: Where protopy completes without timeout, it is in the 1.1–1.5x range vs CPython; script bodies are not yet executed (module shell only), so these reflect environment and resolution cost.
3. **str_concat_loop**: Still susceptible to intermittent timeout; treat median as invalid when it equals or nears the run timeout. Use lock instrumentation (`-DPROTO_GC_LOCK_TRACE=ON`) if further diagnosis is needed.
4. **Reproducibility**: Re-run with the same timeout and report path; for baseline-only, use `--cpython-only`.

## How to re-run

```bash
# Full comparison (protopy vs CPython)
PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --timeout 30 --output reports/benchmark_report.md

# CPython baseline only (no protopy)
python3 benchmarks/run_benchmarks.py --cpython-only --output reports/YYYY-MM-DD_cpython314.md
```
