# list_append_loop.py - Benchmark: lst = []; lst.append(i) for i in range(N)
#
# N matches the protoCpp counterpart (N=10_000).  Set BENCH_N to override.
import os
import time
N = int(os.environ.get("BENCH_N", "10000"))


def main():
    lst = []
    for i in range(N):
        lst.append(i)
    return len(lst)


if __name__ == "__main__":
    t0 = time.perf_counter()
    r = main()
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    print(f"BENCH_RESULT name=list_append_loop N={N} result={r} ms={elapsed_ms:.2f}")
