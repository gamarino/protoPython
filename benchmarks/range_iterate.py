# range_iterate.py - Benchmark: iterate over range(N) counting per element.
#
# N=10_000_000 matches int_sum_loop and the corresponding protoCpp bench.
# Set BENCH_N to override.
import os
import time
N = int(os.environ.get("BENCH_N", "10000000"))


def main():
    n = 0
    for i in range(N):
        n += 1
    return n


if __name__ == "__main__":
    t0 = time.perf_counter()
    r = main()
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=range_iterate N={N} result={r} ms={elapsed_ms:.2f}")
