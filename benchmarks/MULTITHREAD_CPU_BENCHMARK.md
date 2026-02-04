# Multithreaded CPU Benchmark: protoPython vs CPython 3.14

This benchmark compares **wall-clock time** for a fixed amount of CPU-bound work when that work is expressed as a *multithreaded* task.

## What it measures (current setup: single-thread comparison)

- **Same total work:** 4 chunks of `sum(range(50_000))` (CPU-bound, no I/O).
- **Both interpreters** run the script with **`SINGLE_THREAD=1`**, so all 4 chunks run **sequentially in the main thread**. This gives a **fair comparison of per-chunk interpreter speed** without GIL or thread overhead differences.
- **protoPython:** Script runs 4 chunks sequentially (no os/threading; fair per-chunk comparison).
- **CPython 3.14:** Also run with `SINGLE_THREAD=1` so execution model matches (single thread, same total work).


## Interpreting the ratio

| Ratio | Meaning |
|-------|--------|
| **< 1** | protoPython is faster (fewer ms). |
| **= 1** | Same wall time. |
| **> 1** | CPython is faster; the number reflects relative **per-chunk interpreter speed** (e.g. 3.5x means CPython's single-thread execution of the same work is ~3.5x faster). |

This benchmark does **not** yet measure multi-CPU utilization. When protoPython implements real parallel threading, the script can be run without `SINGLE_THREAD=1` and wall time could drop (work split across CPUs).

## How to run

From the project root:

```bash
export PROTOPY_BIN=./build/src/runtime/protopy   # or your protopy path
export CPYTHON_BIN=python3.14                     # optional; default python3
python3 benchmarks/run_benchmarks.py --output reports/multithread_cpu_report.md
```

The **multithread_cpu** row appears in the generated report together with the other benchmarks.

## Reading the report

Example:

```
│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │
├────────────────────────┼──────────────┼──────────────┼──────────────┤
│ multithread_cpu        │       120.00 │       380.00 │ 0.32x faster │
```

- **Ratio &lt; 1** → protoPython is faster (**dramatically less** time).
- **Ratio = 1** → Same wall time.
- **Ratio &gt; 1** → CPython was faster (e.g. due to faster per-chunk execution; multithreaded overhead still applies to CPython).

## Script details

- **Script:** [multithreaded_cpu.py](multithreaded_cpu.py)
- **Env:** Harness sets `SINGLE_THREAD=1` for both. Script has no env dependency; it always runs 4 chunks sequentially for a fair per-chunk comparison.
- **Workload:** 4 × `sum(range(50_000))`; identical total CPU work in both runs.
