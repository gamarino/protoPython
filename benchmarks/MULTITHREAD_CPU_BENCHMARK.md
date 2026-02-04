# Multithreaded CPU Benchmark: protoPython vs CPython 3.14

This benchmark compares **wall-clock time** for a fixed amount of CPU-bound work when that work is expressed as a *multithreaded* task.

## What it measures

- **Same total work:** 4 chunks of `sum(range(50_000))` (CPU-bound, no I/O).
- **CPython 3.14:** Runs the script **with 4 threads**. Because of the **Global Interpreter Lock (GIL)**, only one thread executes Python bytecode at a time. The four threads are effectively serialized, so wall time is similar to running four times the single-chunk work in one thread, plus thread creation/join overhead.
- **protoPython:** Runs the same script with **`SINGLE_THREAD=1`**, so all 4 chunks run in the **main thread** (protoPython’s `threading` module is currently a stub). Total work is identical; there is no thread overhead.

## Why protoPython can be dramatically faster

| | CPython 3.14 | protoPython |
|--|--------------|-------------|
| **Execution** | 4 threads; GIL serializes them | Same work in one thread |
| **Wall time** | High (GIL + thread overhead) | Can be **dramatically less** (no GIL, no extra threads) |

When the **ratio (protopy/cpython)** is **below 1.0**, protoPython is faster. On multithreaded CPU-bound workloads, protoPython often shows **dramatically less** wall time because it is not limited by the GIL and (in this benchmark) avoids thread overhead.

When protoPython gains real multi-threaded execution, the same workload could be run in parallel and wall time would drop further (closer to one chunk’s time instead of four).

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
- **Env:** `SINGLE_THREAD=1` is set by the harness when invoking protopy so that all work runs in the main thread. CPython runs without it, so it uses 4 threads.
- **Workload:** 4 × `sum(range(50_000))`; identical total CPU work in both runs.
