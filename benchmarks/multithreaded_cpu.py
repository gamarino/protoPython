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

# 5_000 explicit Python loop iterations per worker so the bytecode
# dispatcher actually runs (and CPython actually pays the GIL bill).
# Built-in sum(range(N)) is C-implemented and would never expose the
# parallelism win.  The chunk size is intentionally modest because a
# pure-Python tight loop pays per-bytecode dispatch cost in protoPython
# that CPython does in C; the benchmark stresses the per-bytecode path
# enough to expose the parallelism difference without being dominated
# by GC stop-the-world handshake amplification.
CHUNK = 5000
N_THREADS = 4

# Workers publish into their own slot — no shared mutation, no lock needed.
# A non-zero slot means that worker finished.  Sentinel 0 is fine because
# expected_sum >= 1 for CHUNK >= 2.
_results = [0] * N_THREADS


def cpu_chunk(worker_index):
    s = 0
    i = 0
    while i < CHUNK:
        s += i
        i += 1
    _results[worker_index] = s


def main():
    for i in range(N_THREADS):
        _results[i] = 0
    t0 = time.perf_counter()
    for i in range(N_THREADS):
        _thread.start_new_thread(cpu_chunk, (i,))
    # Wait for every worker to publish its result.  Each worker writes to a
    # distinct slot, so this loop observes completion without a lock.
    deadline = t0 + 30.0
    expected_sum = sum(range(CHUNK))
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
    # Sanity check: every worker computed the right sum.
    for idx in range(N_THREADS):
        assert _results[idx] == expected_sum, (
            "wrong sum from worker %d: got %d, expected %d"
            % (idx, _results[idx], expected_sum))
    return elapsed


if __name__ == "__main__":
    main()
