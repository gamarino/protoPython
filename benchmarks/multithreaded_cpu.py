# multithreaded_cpu.py - Benchmark: 4 chunks of CPU-bound work, multithreaded.
#
# protoPython: real multithreading (ProtoSpace, GIL-less) - can use multiple cores.
# CPython: threads with GIL - CPU-bound work serialized by GIL, wall time ~ single-thread.
# Same total work: 4 x sum(range(CHUNK)). See MULTITHREAD_CPU_BENCHMARK.md.

CHUNK = 50000
N_THREADS = 4


def cpu_chunk(worker_index=None):
    """One unit of CPU-bound work. Logs pid/tid when threading available."""
    if worker_index is not None:
        try:
            import _thread
            _thread.log_thread_ident("worker %d" % worker_index)
        except Exception:
            pass
    return sum(range(CHUNK))


def main():
    import sys
    try:
        sys.stdout.write("main_entered\n")
        sys.stdout.flush()
    except Exception:
        pass
    # Always try to log pid/tid from main thread (diagnostic).
    try:
        import _thread
        _thread.log_thread_ident("main_diag")
    except Exception:
        pass
    try:
        import threading
        has_thread = getattr(threading, "_has_thread", False)
    except ImportError:
        has_thread = False
    try:
        sys.stdout.write("has_thread=%s\n" % has_thread)
        sys.stdout.flush()
    except Exception:
        pass
    if has_thread:
        try:
            import _thread
            _thread.log_thread_ident("main")
        except Exception:
            pass
        threads = []
        for i in range(N_THREADS):
            t = threading.Thread(target=cpu_chunk, args=(i,))
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
    else:
        for _ in range(N_THREADS):
            cpu_chunk(None)
    return 0


if __name__ == "__main__":
    main()
