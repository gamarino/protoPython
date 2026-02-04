# multithreaded_cpu.py - Benchmark: same CPU work, single-thread (fair comparison)
#
# Execution model:
# - Harness sets SINGLE_THREAD=1 for BOTH interpreters so both run 4 chunks in the
#   main thread. This gives a fair comparison (same work, same execution model).
# - CPython: with GIL, even 4 threads would serialize to one CPU; running in main
#   avoids thread overhead and measures single-thread throughput.
# - protoPython: threading is currently a stub (Thread.start/join are no-ops), so
#   we must run in main. protoPython is designed to be GIL-free; when real threading
#   is implemented, it could run 4 chunks in parallel (multi-CPU) and wall time
#   would drop toward one chunk's time.
#
# Same total work: 4 x sum(range(CHUNK)).

import os

CHUNK = 50000
N_THREADS = 4


def cpu_chunk():
    """One unit of CPU-bound work."""
    return sum(range(CHUNK))


def main():
    use_threads = os.environ.get("SINGLE_THREAD", "").strip() != "1"
    if use_threads:
        import threading
        threads = []
        for _ in range(N_THREADS):
            t = threading.Thread(target=cpu_chunk)
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
    else:
        for _ in range(N_THREADS):
            cpu_chunk()
    return 0


if __name__ == "__main__":
    main()
