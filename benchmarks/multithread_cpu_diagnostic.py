#!/usr/bin/env python3
"""
Diagnostic benchmark for multithread_cpu: variable CHUNK and N_THREADS
to determine whether execution is serialized and where time is spent.

Usage:
  PROTOPY_BIN=./build/src/runtime/protopy python3 multithread_cpu_diagnostic.py
  CHUNK=200000 N_THREADS=4 PROTOPY_BIN=... python3 multithread_cpu_diagnostic.py

Expectations (no hacks):
- Worst case (fully serialized): wall_time ≈ N_THREADS × single_chunk_time.
- Best case (parallel): wall_time ≈ single_chunk_time.
- If wall_time >> N_THREADS × single_chunk_time, something else is wrong
  (e.g. excessive lock contention, repeated work, or blocking).
"""

import os
import sys
import time

# Config from env for easy tuning without editing
CHUNK = int(os.environ.get("CHUNK", "50000"))
N_THREADS = int(os.environ.get("N_THREADS", "4"))


def cpu_chunk(worker_index=None):
    """One unit of CPU-bound work: sum(range(CHUNK))."""
    return sum(range(CHUNK))


def run_sequential():
    """Run N_THREADS chunks one after another (no threads). Baseline for serialized time."""
    start = time.perf_counter()
    for _ in range(N_THREADS):
        cpu_chunk(None)
    return (time.perf_counter() - start) * 1000


def run_threaded():
    """Run N_THREADS chunks in parallel (threading.Thread)."""
    try:
        import threading
        has_thread = getattr(threading, "_has_thread", False)
    except ImportError:
        has_thread = False
    if not has_thread:
        return run_sequential()  # fallback
    start = time.perf_counter()
    threads = []
    for i in range(N_THREADS):
        t = threading.Thread(target=cpu_chunk, args=(i,))
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    return (time.perf_counter() - start) * 1000


def run_single_chunk():
    """Run one chunk only. Establishes single-chunk time."""
    start = time.perf_counter()
    cpu_chunk(None)
    return (time.perf_counter() - start) * 1000


def main():
    out_path = os.environ.get("DIAG_OUT", "/tmp/multithread_diag_result.txt")
    out_dir = os.path.dirname(out_path)
    if out_dir:
        try:
            os.makedirs(out_dir, exist_ok=True)
        except Exception:
            pass
    # Write immediately so we know main() ran
    try:
        with open(out_path, "w") as f:
            f.write("main_started\n")
    except Exception:
        pass
    lines = []

    single_ms = run_single_chunk()
    seq_ms = run_sequential()
    thr_ms = run_threaded()

    expected_serial_ms = N_THREADS * single_ms
    ratio = (thr_ms / expected_serial_ms) if expected_serial_ms > 0 else 0

    lines.append(f"CHUNK={CHUNK} N_THREADS={N_THREADS}")
    lines.append(f"single_chunk_ms={single_ms:.2f}")
    lines.append(f"sequential_ms={seq_ms:.2f} (expected_serial~{expected_serial_ms:.1f})")
    lines.append(f"threaded_ms={thr_ms:.2f}")
    lines.append(f"ratio_threaded_over_expected_serial={ratio:.2f}")
    if ratio > 2.0:
        lines.append("INCOHERENT: threaded >> expected_serial (should be <= ~1.0 if serialized)")
    text = "\n".join(lines) + "\n"
    with open(out_path, "w") as f:
        f.write(text)
    for line in lines:
        print(line, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
