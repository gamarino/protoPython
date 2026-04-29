"""
Recursive Fibonacci — pure function call and SmallInt benchmark.

Stresses: CALL_FUNCTION, BINARY_ADD, COMPARE_OP, RETURN_VALUE.
No data structures — only SmallInt arithmetic and recursion.
Fair across implementations: semantics are identical; no CPython-specific
data-structure optimisations (specialising caches, list C-array mutations)
are relevant here.

Usage:  python3 bench_fib.py          # default N=25
        python3 bench_fib.py 28       # heavier
"""
import sys
import time


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def main():
    n = 25
    if len(sys.argv) > 1:
        n = int(sys.argv[1])

    fib(n)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        result = fib(n)
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    print("fib n=%d  min=%.1fms  median=%.1fms  result=%d" %
          (n, times[0], times[2], result))


if __name__ == "__main__":
    main()
