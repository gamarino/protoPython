# int_sum_loop.py - Benchmark: sum(range(N))
#
# N defaults to 100000 (matching the canonical harness baseline since 2026-05).
# Set BENCH_N=<int> in the environment to override — the protoCpp comparison
# at <https://github.com/gamarino/protoCpp> uses BENCH_N=10000000 so the loop
# body dominates the ~3 ms binary startup floor.
import os
N = int(os.environ.get("BENCH_N", "100000"))

def main():
    return sum(range(N))

if __name__ == "__main__":
    main()
