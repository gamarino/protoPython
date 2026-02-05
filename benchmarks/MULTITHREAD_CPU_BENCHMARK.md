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

3. **Allocate only one batch when multi-threaded (critical fix)**  
   When `runningThreads > 1` and allocating from the OS, allocate only `batchSize` cells (e.g. 60k), not 256k. Previously we allocated 256k and chained all 256k cells *while holding the global lock*, so one thread held the lock for hundreds of ms and serialized all others. Allocating a single batch per thread keeps the critical section short so worst case is roughly serialized execution (~N_THREADS × single_chunk_time).

4. **Correct `runningThreads` count**  
   Removed the duplicate `runningThreads++` in `ProtoSpace::newThread()` (the increment is already done in `ProtoThreadImplementation` constructor), so batch-size scaling uses the correct thread count.

   **OS allocation cap:** Each request to the OS in `getFreeCells` is capped at 16 MiB per call (see protoCore `docs/GarbageCollector.md`). This limits single `posix_memalign` size and keeps critical sections bounded.

5. **Lock-free `submitYoungGeneration` (major fix)**  
   Every context destruction (every function return) used to take `globalMutex` to push a dirty segment onto `dirtySegments`. With 4 threads and hundreds of thousands of returns, this serialized all threads on the same lock. `dirtySegments` is now a lock-free stack: `submitYoungGeneration` pushes with an atomic compare-exchange (no lock). The GC drains the stack under its existing lock when it runs. This removes the main hot-path contention and allows threads to run in parallel.

**Result:** multithread_cpu ratio improves from ~70× to ~3.3× slower (e.g. 215 ms vs 65 ms CPython). Threads now run in parallel with much less contention. Remaining gap is due to: (a) shared allocator—`getFreeCells` still takes the lock when a thread exhausts its batch; (b) possible shared caches in protoPython (e.g. resolve cache); (c) single-threaded interpreter throughput. To get protoPython *faster* than CPython on this workload, per-thread heaps (LocalHeap in protoCore) and reduced shared-state contention are needed (see [REARCHITECTURE_PROTOCORE.md](../docs/REARCHITECTURE_PROTOCORE.md)).
