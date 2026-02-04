# multithreaded_cpu.py - Benchmark: same CPU work, 4 chunks.
#
# Both interpreters run with SINGLE_THREAD=1: 4 chunks run sequentially in the main thread.
# Same total work: 4 x sum(range(CHUNK)). Fair per-chunk comparison (see MULTITHREAD_CPU_BENCHMARK.md).

CHUNK = 50000
N_THREADS = 4


def cpu_chunk(worker_index=None):
    """One unit of CPU-bound work."""
    return sum(range(CHUNK))


def main():
    for _ in range(N_THREADS):
        cpu_chunk(None)
    return 0


if __name__ == "__main__":
    main()
