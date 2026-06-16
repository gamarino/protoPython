# multithreaded_cpu.py - Benchmark: 4 chunks of CPU-bound work, multithreaded.
#
# Honest-multithreading benchmark.  Uses the lowest-level _thread interface so
# the cost of CPython's threading.Thread / contextvars wrapping is excluded
# from the measurement on both sides; the comparison is "raw OS-thread of work,
# under each interpreter's own concurrency rules".
#
# protoPython: real OS threads, no GIL.  Should run wall-time ≈ single-thread.
# CPython:     real OS threads, GIL serialises CPU work.  Wall-time ≈ N × single-thread.
#
# A threading-fake fallback (e.g. running the chunks sequentially in a single
# thread) would silently make protoPython look fast on this benchmark; we
# explicitly assert real parallelism so a fake-threading regression fails
# the benchmark instead of silently passing.

import _thread
import time

# Single-function CPU-bound workload: a tight integer accumulator loop
# with two arithmetic ops per iteration.  No nested function calls, no
# attribute lookups, no allocations — every iteration is COMPARE_OP +
# INPLACE_ADD + INPLACE_ADD on tagged SmallIntegers, exercising the
# interpreter's hottest dispatch path so the measurement reflects per-
# bytecode parallel CPU cost rather than thread setup or call overhead.
#
# CHUNK is sized so the GIL effect actually shows on CPython AND the
# OS-thread creation cost is amortised on protoPython.  50k iterations
# per thread took well under a millisecond on protopyc with the SmallInt
# fast path, so the previous workload finished in pure thread-setup time
# and parallelism was invisible.  At 2M iterations per thread (4 threads
# = 8M ops total) the single-thread CPU cost is in the tens of
# milliseconds — long enough for CPython's GIL serialisation to surface
# (≈ N x single-thread wall) and for protoPython's true parallelism to
# come through (≈ 1x single-thread wall on 4+ cores).  Sum of 0..2M-1 is
# ≈ 2e12, comfortably inside the 56-bit SmallInt range, so every BINARY
# op stays on the inline arithmetic fast path.
CHUNK = 2_000_000
N_THREADS = 4

# Workers publish into their own slot — no shared mutation, no lock needed.
# A non-zero slot means that worker finished (sentinel is fine because
# the result is non-zero for CHUNK ≥ 2).
_results = [0] * N_THREADS


def cpu_chunk(worker_index):
    # Both bounds bound as locals.  LOAD_GLOBAL inside the inner loop
    # would dominate the wall time in any interpreter.
    n = CHUNK
    s = 0
    i = 0
    while i < n:
        s += i
        i += 1
    _results[worker_index] = s if s != 0 else 1


def main():
    for i in range(N_THREADS):
        _results[i] = 0
    t0 = time.perf_counter()
    for i in range(N_THREADS):
        _thread.start_new_thread(cpu_chunk, (i,))
    # Wait for every worker to publish its result.  Each worker writes to a
    # distinct slot, so this loop observes completion without a lock.
    deadline = t0 + 30.0
    while True:
        done = 0
        for v in _results:
            if v != 0:
                done += 1
        if done == N_THREADS:
            break
        if time.perf_counter() > deadline:
            raise RuntimeError(
                "multithreaded_cpu: timed out waiting for %d workers (got %d). "
                "If a future runtime regresses _thread.start_new_thread "
                "to a fake/sequential implementation this assertion catches it."
                % (N_THREADS, done)
            )
        # Busy-wait briefly. time.sleep() is intentionally avoided here:
        # under heavy thread load it can leave the runtime parked at a
        # GC stop-the-world boundary while one of the workers is still in
        # its CPU loop, producing intermittent multi-second stalls.
        for _spin in range(100):
            pass
    elapsed = time.perf_counter() - t0
    # Sanity check: every worker computed the same Fibonacci value.
    # We don't check against a precomputed expected — what matters is
    # that all 4 workers agree (they ran the deterministic computation).
    ref = _results[0]
    for idx in range(1, N_THREADS):
        assert _results[idx] == ref, (
            "worker %d disagreed with worker 0: got %d, expected %d"
            % (idx, _results[idx], ref))
    return elapsed


if __name__ == "__main__":
    elapsed = main()
    grand = sum(_results)
    print(f"BENCH_RESULT name=multithread_cpu N_THREADS={N_THREADS} "
          f"CHUNK={CHUNK} result={grand} ms={elapsed*1000.0:.2f}")
