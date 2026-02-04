# multithreaded_cpu.py - Benchmark: same CPU work, 4 chunks.
#
# Execution model:
# - protoPython: runs with threading (4 ProtoSpace threads in parallel, no GIL).
# - CPython: harness sets SINGLE_THREAD=1 so 4 chunks run in main thread (GIL
#   would serialize threads anyway; single-thread keeps comparison stable).
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
