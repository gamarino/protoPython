# Benchmark Analysis: CPython 3.14 Baseline (2025-02-04)

## Objective

Run the protoPython benchmark suite using **CPython 3.14** as the reference implementation, record results, and document findings for future comparison once protoPython startup is stable.

## Methodology

- **Harness**: `benchmarks/run_benchmarks.py` (see [IMPLEMENTATION_PLAN.md](../docs/IMPLEMENTATION_PLAN.md) §7).
- **Reference**: CPython 3.14 (`python3.14`).
- **Runs**: Median of 5 runs after 2 warm-up runs per benchmark.
- **Mode**: `--cpython-only` was used because protoPython currently hits a known startup/GC hang when running `protopy --module abc` (see [TESTING.md](../docs/TESTING.md)). No protoPython times were collected.

## Results Summary

| Benchmark         | CPython 3.14 (median ms) | Workload / N |
|-------------------|---------------------------|--------------|
| startup_empty     | 48.22                     | `import abc` |
| int_sum_loop      | 22.42                     | sum(range(100000)) |
| list_append_loop  | 21.76                     | list append loop, N=10000 |
| str_concat_loop   | 21.61                     | string concat loop, N=10000 |
| range_iterate     | 21.48                     | iterate range(100000) |

Raw report: [2025-02-04_cpython314.md](2025-02-04_cpython314.md).

*Note: Full benchmark (protopy vs CPython) was re-run; protopy timed out (60s) on `list_append_loop`, so this commit records CPython 3.14 baseline only via `--cpython-only`.*

## Observations

1. **Startup** (~48 ms): Importing a minimal module (`abc`) on this machine gives a baseline for interpreter startup. protoPython is not measured when it times out.
2. **Arithmetic / iteration**: `int_sum_loop` and `range_iterate` are in the 21–22 ms range; dominated by loop and integer operations.
3. **Allocation-heavy**: `list_append_loop` (~22 ms) and **string** `str_concat_loop` (~22 ms) are in line with the other script benchmarks.
4. **Variance**: Median times can vary by run and load; use the same host and `--cpython-only` for reproducible baseline.

## Why protoPython Was Not Measured

The full benchmark (protoPython vs CPython) was not run because:

- `protopy --module abc` exceeds the 60 s timeout (documented in [TESTING.md](../docs/TESTING.md) as a suspected GC deadlock during `PythonEnvironment`/list prototype creation).
- The harness now supports `--cpython-only` so that a CPython 3.14 baseline can be recorded and reported when protoPython is unavailable or unstable.

## Conclusions and Next Steps

- **Baseline**: The table above establishes a CPython 3.14 baseline on this host (Linux x86_64) for the five standard benchmarks. It can be reused for comparison when protoPython runs without hanging.
- **Reproducibility**: Re-run with:
  ```bash
  CPYTHON_BIN=python3.14 python3.14 benchmarks/run_benchmarks.py --cpython-only --output reports/YYYY-MM-DD_cpython314.md
  ```
- **Full comparison**: After addressing the foundation test/startup hang (Phase 2b in IMPLEMENTATION_PLAN), run without `--cpython-only` and with `PROTOPY_BIN` set to obtain ratio and geometric mean (protopy vs cpython) as in §7.2 of IMPLEMENTATION_PLAN.
