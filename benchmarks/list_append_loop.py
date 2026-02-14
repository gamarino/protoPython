# list_append_loop.py - Benchmark: lst = []; lst.append(i) for i in range(N)
import os
N = int(os.environ.get("BENCH_N", "10000"))

def main():
    lst = []
    for i in range(N):
        lst.append(i)
    return len(lst)

if __name__ == "__main__":
    result = main()
    # import sys
    # sys.stdout.write("result=%d\n" % result)
    # sys.stdout.flush()
    print("done loop")
