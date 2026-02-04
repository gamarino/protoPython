# Startup / GC Hang: Analysis and Resolution

This document describes the analysis of the foundation test and protopy startup hang, the root cause (GC deadlock in protoCore), and how to verify the fix.

## 1. Symptom (Historical)

- `test_foundation`, `test_regr`, and `protopy --module abc` could hang during initialization, typically when creating the list prototype or during early allocations.
- The hang occurred inside protoCore’s allocation/GC path (e.g. in `getFreeCells` or while waiting for the GC thread).
- CTest was given a 60s timeout so the suite would fail instead of hanging indefinitely.

## 2. Root Cause: GC Deadlock in protoCore

The GC in protoCore uses a stop-the-world (STW) protocol:

1. **Application thread** that needs cells calls `ProtoSpace::getFreeCells()`. If the global free list is empty, it must trigger GC and then **park** (increment `parkedThreads`, wait on `gcCV` until `!gcStarted`) so that the GC thread can run.
2. **GC thread** waits on `gcCV` until `gcStarted` is true, then waits until `parkedThreads >= runningThreads` before running the collection.

**Deadlock scenario (before fix):** If the application thread triggered GC (set `gcStarted`, notified the GC thread) but then **left** `getFreeCells` without parking (e.g. by taking a different path that allocated from the OS and returned), the GC thread would wake, see `gcStarted`, then wait forever for `parkedThreads >= runningThreads` because the application thread never incremented `parkedThreads`. Conversely, the application thread might have been waiting for GC to finish while GC was waiting for that thread to park.

**Fix (in protoCore):** In `getFreeCells`:

- **When free list is non-empty:** Return a batch from the list; do **not** trigger GC. So no “trigger and leave” path.
- **When free list is empty:** Set `gcStarted`, notify GC, then **always** (if `gcThread` exists and caller is not the GC thread) increment `parkedThreads`, notify, and wait on `gcCV` until `!gcStarted`. Only after that wait does the thread re-check the free list or fall through to requesting memory from the OS.
- **When allocating from the OS** (after the wait or when there is no GC thread): Do **not** trigger GC before returning; the comment in code states that triggering here would cause a deadlock because the thread would return without parking.

So the invariant is: **whenever GC is triggered, the triggering thread parks and waits for GC to finish.** That guarantees the GC thread eventually sees `parkedThreads >= runningThreads` and can complete, then clear `gcStarted` and notify waiters.

Reference: protoCore commit `fix(gc): resolve getFreeCells deadlock with GC thread` (or equivalent logic in `ProtoSpace::getFreeCells`).

## 3. protoPython’s Role: mainContext

For the GC to treat protoPython’s allocations as roots, the environment’s context must be the space’s **main context** when allocations happen.

- `PythonEnvironment` holds a `proto::ProtoSpace space` and creates a single root context with `context = new proto::ProtoContext(&space)` (no parent, no thread).
- `ProtoContext` constructor, when `thread` is null and `space` is non-null, sets `space->mainContext = this`. So the first (and only) application context created by protoPython becomes the main context.
- During STW, the GC scans `space->mainContext` (and its `lastAllocatedCell` chain and locals). So all objects allocated via `PythonEnvironment::context` are reachable from that root set and are not collected while still in use.

No change is required in protoPython for this; the existing `new ProtoContext(&space)` call is correct. The important point is that **no other code must overwrite `mainContext`** for the duration of environment setup and execution without ensuring the environment’s context is again registered (e.g. as main or via another root).

## 4. Verification

To confirm that the startup hang is resolved:

1. **Build** protoPython (and protoCore) and ensure the protoCore tree includes the `getFreeCells` fix above.
2. **Run the minimal reproducer:**
   ```bash
   ./build/test/library/test_minimal
   ```
   Expected: prints “Creating PythonEnvironment…”, “C: PythonEnvironment created.”, “OK: BasicTypesExist” and exits in a few seconds.
3. **Run protopy:**
   ```bash
   ./build/src/runtime/protopy --module abc
   ```
   Expected: exits with code 0 in a few seconds.
4. **Run the full benchmark** (optional):
   ```bash
   PROTOPY_BIN=./build/src/runtime/protopy CPYTHON_BIN=python3.14 python3.14 benchmarks/run_benchmarks.py --output reports/full.md
   ```
   Expected: all five benchmarks complete and the report is written.

If any of these hang (e.g. beyond 60s), the protoCore in use likely does not contain the getFreeCells deadlock fix, or a different concurrency bug is present; the first place to audit is `ProtoSpace::getFreeCells` and the GC thread loop (park/wake and `gcStarted` / `parkedThreads` / `runningThreads`).

## 5. Summary

- **Cause:** GC deadlock when an application thread triggered GC in `getFreeCells` but did not park and wait for GC to finish.
- **Fix:** In protoCore, ensure that whenever the free list is empty and GC is triggered, the calling thread parks (increments `parkedThreads`, waits on `gcCV` until `!gcStarted`) before re-checking the free list or allocating from the OS. No “trigger then return without parking” path.
- **protoPython:** Uses a single `ProtoContext` for the environment; the `ProtoContext` constructor sets `space->mainContext = this`, so GC correctly roots the environment’s allocations. No application-level workaround or hack is required.
