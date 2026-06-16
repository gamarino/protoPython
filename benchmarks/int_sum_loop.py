# int_sum_loop.py - Benchmark: sum(range(N))
#
# N matches the protoCpp bench (N=10_000_000) so wall-clocks are directly
# comparable to <https://github.com/gamarino/protoCpp>'s int_sum_loop.
# Set BENCH_N=<int> in the environment to override.
import os
import time
N = int(os.environ.get("BENCH_N", "10000000"))


def main():
    s = 0
    for i in range(N):
        s += i
    return s


if __name__ == "__main__":
    t0 = time.perf_counter()
    r = main()
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=int_sum_loop N={N} result={r} ms={elapsed_ms:.2f}")
