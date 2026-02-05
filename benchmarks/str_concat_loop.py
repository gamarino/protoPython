# str_concat_loop.py - Benchmark: s = ""; s = s + "x" for i in range(N)
# N kept moderate so protoPython completes within timeout (loop is O(N^2) per concat).
import os
N = int(os.environ.get("BENCH_N", "2000"))

def main():
    s = ""
    for i in range(N):
        s = s + "x"
    return len(s)

if __name__ == "__main__":
    main()
