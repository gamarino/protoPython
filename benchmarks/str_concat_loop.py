# str_concat_loop.py - Benchmark: s = ""; s = s + "x" for i in range(N)
#
# N=2000 matches the protoCpp counterpart.  Loop is conceptually O(N^2)
# in implementations without a rope; protoPython uses ropes so it stays
# linear.  Set BENCH_N to override.
import os
import time
N = int(os.environ.get("BENCH_N", "2000"))


def main():
    s = ""
    for i in range(N):
        s = s + "x"
    return len(s)


if __name__ == "__main__":
    t0 = time.perf_counter()
    r = main()
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=str_concat_loop N={N} result={r} ms={elapsed_ms:.2f}")
