# attr_lookup.py - Benchmark: tight loop reading three attributes of an
# object N times each.  Mirrors protoCpp's attr_lookup (default N=5M).
# Pass an int argument or set BENCH_N to override.
import os
import sys
import time


class FastObject:
    def __init__(self, a, b, c):
        self.a = a
        self.b = b
        self.c = c


def run_bench(n):
    obj = FastObject(1, 2, 3)
    total = 0
    for _ in range(n):
        total += obj.a
        total += obj.b
        total += obj.c
    return total


if __name__ == "__main__":
    n = int(os.environ.get("BENCH_N", "5000000"))
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    t0 = time.perf_counter()
    r = run_bench(n)
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=attr_lookup N={n} result={r} ms={elapsed_ms:.2f}")
