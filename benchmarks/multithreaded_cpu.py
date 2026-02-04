# multithreaded_cpu.py - Benchmark: CPU-bound work, multithreaded (CPython) vs single-thread (protoPython)
# Same total work: 4 chunks of sum(range(CHUNK)). CPython runs 4 threads (GIL serializes).
# protoPython: set SINGLE_THREAD=1 to run 4 chunks in main (threading is stubbed).
# Expectation: CPython multithreaded wall time is high (GIL); protoPython single-thread same work can be less.

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
