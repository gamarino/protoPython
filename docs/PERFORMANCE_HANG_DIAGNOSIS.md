# Performance Test Hang: Diagnosis and Mitigation

This document describes the intermittent hang observed when running the benchmark suite with `protopy` (script-based benchmarks) and how to obtain reliable reports.

## 1. Symptom

- When running `benchmarks/run_benchmarks.py` with `PROTOPY_BIN` set, **protopy** can **time out** (default 60s) on script benchmarks such as `list_append_loop.py`, `str_concat_loop.py`, or others.
- The hang is **intermittent**: the same script may complete in under a second in one run and hang in a later run (e.g. run 9 of 10 in a loop).
- Each benchmark run is a **separate process**; the hang occurs inside a single `protopy` process, not across processes.

## 2. Relation to Startup GC Hang

The **startup/GC deadlock** (during `PythonEnvironment` construction or `protopy --module abc`) was addressed in **protoCore** by fixing `ProtoSpace::getFreeCells`: when the free list is empty, the thread that triggers GC now **parks and waits** for the GC thread to finish, so the GC thread can observe `parkedThreads >= runningThreads` and complete. See [STARTUP_GC_ANALYSIS.md](STARTUP_GC_ANALYSIS.md).

The **performance-test hang** was traced with lock instrumentation (build with `-DPROTO_GC_LOCK_TRACE=ON`). The trace showed `getFreeCells REL(return OS)` followed by `gcLoop ACQ(gcStarted)`: the main thread had set `gcStarted` and returned without parking (because `gcThread` was still null during early allocation, e.g. in `ProtoSpace` ctor). When the GC thread later started, it saw `gcStarted` and then blocked forever waiting for `parkedThreads >= runningThreads` (main had never parked). **Fix (in protoCore):** set `gcStarted` and notify the GC thread only when we are about to park (i.e. only when `gcThread` exists and the caller is not the GC thread). If `gcThread` is null, do not set `gcStarted`; fall through to OS allocation. This removes the deadlock in the “early allocation before GC thread exists” scenario.

## 3. Reproducing

1. Build: `cd build && cmake .. && cmake --build . --target protopy`
2. Run one script repeatedly with a short timeout:
   ```bash
   for i in 1 2 3 4 5 6 7 8 9 10; do
     echo "Run $i"
     timeout 8 ./build/src/runtime/protopy --path benchmarks --script "$(pwd)/benchmarks/str_concat_loop.py" || exit 1
   done
   ```
   A hang may appear after several runs (e.g. run 9).
3. Full benchmark suite:
   ```bash
   PROTOPY_BIN=./build/src/runtime/protopy python3 benchmarks/run_benchmarks.py --output reports/out.md
   ```
   If protopy hangs on any benchmark, that run will hit the 60s timeout and the script will continue with the next benchmark (or exit with timeout on that subprocess).

## 4. Mitigations (No Hacks)

- **Configurable timeout**: Use `--timeout SECS` to reduce the wait when a run hangs (e.g. `--timeout 15` for faster failure).
- **CPython-only baseline**: Use `--cpython-only` to run only the CPython side and produce a baseline report when protopy is unreliable:
  ```bash
  python3 benchmarks/run_benchmarks.py --cpython-only --output reports/cpython_baseline.md
  ```
- **Verification**: Ensure **protoCore** includes the `getFreeCells` deadlock fix (see [STARTUP_GC_ANALYSIS.md](STARTUP_GC_ANALYSIS.md)). Without it, startup and script runs can hang more frequently.

No application-level workaround (e.g. disabling GC or skipping allocations) is recommended; the correct fix remains in protoCore’s GC protocol and any remaining concurrency bug should be fixed there or identified with further instrumentation.

## 5. Lock instrumentation (diagnostic)

To see exactly where the main and GC threads acquire/release `globalMutex`, build **protoCore** (and then protoPython) with GC lock tracing enabled:

```bash
# From protoPython build (protoCore is a subdirectory)
cd build && cmake .. -DPROTO_GC_LOCK_TRACE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target protopy
```

Then run the failing scenario and capture stderr:

```bash
./build/src/runtime/protopy --path benchmarks --script "$(pwd)/benchmarks/str_concat_loop.py" 2>gc-trace.log
```

Each line is `[gc-lock] <thread_id> <event>`. Events:

- **getFreeCells**: `ACQ`, `REL(park)`, `ACQ(wake)`, `REL(return batch|return after GC|return OS)`
- **gcLoop**: `ACQ(init)`, `ACQ(gcStarted)`, `ACQ(parked)`, `REL(mark-sweep)`, `ACQ(freeList)` / `REL(freeList)`, `ACQ(after-sweep)`
- **allocCell STW**: `ACQ`, `ACQ(wake)`, `REL`
- **submitYoung**: `ACQ`, `REL`

When a run hangs, the **last lines** of the trace show which thread was holding or waiting for the lock (e.g. main stuck before `ACQ(wake)` or GC stuck before `ACQ(parked)`). Disable tracing for normal use (omit `-DPROTO_GC_LOCK_TRACE=ON` and reconfigure).

## 6. Summary

| Item | Description |
|------|-------------|
| **Symptom** | Intermittent timeout/hang when running script benchmarks with protopy. |
| **Cause** | Not fully isolated; GC protocol fix in protoCore addresses the known deadlock; intermittent hang may be a separate race. |
| **Mitigation** | Use `--timeout` and/or `--cpython-only` for stable benchmark reports; ensure protoCore has the getFreeCells fix. |
| **Diagnostic** | Build with `-DPROTO_GC_LOCK_TRACE=ON` and capture stderr to see lock order when a hang occurs. |
