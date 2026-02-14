# range_iterate.py - Benchmark: iterate over range(N) with N=100000
import os
print("DEBUG: os is", os)
print("DEBUG: type(os) is", type(os))
N = int(os.environ.get("BENCH_N", "100000"))


def main():
    n = 0
    for i in range(N):
        n += 1
    return n


if __name__ == "__main__":
    main()
