# call_recursion.py - Benchmark: fib(N).  Mirrors protoCpp's
# call_recursion (fib(25)).  Pass an int argument or set BENCH_N to
# override.
import os
import sys
import time


def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


def run_bench(n):
    return fib(n)


if __name__ == "__main__":
    n = int(os.environ.get("BENCH_N", "25"))
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    t0 = time.perf_counter()
    r = run_bench(n)
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=call_recursion N={n} result={r} ms={elapsed_ms:.2f}")
