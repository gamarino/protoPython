# Multithreaded CPU Benchmark: protoPython vs CPython 3.14

This benchmark compares **wall-clock time** for a fixed amount of CPU-bound work when that work is expressed as a *multithreaded* task.

## What it measures (multithreading comparison)

- **Same total work:** 4 chunks of `sum(range(50_000))` (CPU-bound, no I/O).
- **protoPython:** Real multithreading (ProtoSpace threads, GIL-less). Work can run in parallel on multiple cores.
- **CPython 3.14:** Uses threads, but the GIL serializes CPU-bound Python bytecode. Wall time ~ single-thread.


## Interpreting the ratio

| Ratio | Meaning |
|-------|--------|
| **< 1** | protoPython is faster (GIL-less parallelism shows benefit). |
| **= 1** | Same wall time. |
| **> 1** | CPython faster (e.g. per-chunk overhead dominates; or protoPython thread overhead). |

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
- **Execution:** Both use `threading.Thread` when available. protoPython uses ProtoSpace threads (GIL-less); CPython uses OS threads (GIL serializes CPU-bound work).
- **Workload:** 4 × `sum(range(50_000))`; identical total CPU work in both runs.

---

## Debugging and root cause (protoCore allocator)

**Observed:** protoPython multithread_cpu was ~70× slower than CPython (e.g. 2300 ms vs 32 ms). All benchmark runs completed (no timeouts).

**Root cause:** All threads share a single global free list in `ProtoSpace`. `getFreeCells()` takes `globalMutex` to take a batch of cells. With 4 workers each doing ~50k allocations, every refill (batch exhaustion) required the lock, so threads serialized on the allocator. Additionally, when the global list was empty, threads would park for stop-the-world GC; with the main thread blocked in `join()`, that path could cause long pauses or contention.

**Fixes applied (in protoCore):**

1. **Larger batch when multiple threads run**  
   In `getFreeCells()`, when `runningThreads > 1`, the batch size is scaled (and clamped to at least 60k, max 64k) so each thread refills less often and holds the lock fewer times.

2. **Skip park-for-GC when multi-threaded**  
   When `runningThreads > 1` and the global list is empty, skip the “trigger GC and park” path and go directly to OS allocation. This avoids stop-the-world pauses while the main thread is in `join()` and reduces serialization.

3. **Larger OS allocation when multi-threaded**  
   When allocating from the OS with multiple threads, use a larger multiplier so the global list is refilled with more cells and subsequent threads rarely need to trigger allocation.

4. **Correct `runningThreads` count**  
   Removed the duplicate `runningThreads++` in `ProtoSpace::newThread()` (the increment is already done in `ProtoThreadImplementation` constructor), so batch-size scaling uses the correct thread count.

**Result:** multithread_cpu completes without timeout; ratio improves (e.g. from ~70× to ~33× slower). Full parallelism would require per-thread heaps (see [REARCHITECTURE_PROTOCORE.md](../docs/REARCHITECTURE_PROTOCORE.md)).
