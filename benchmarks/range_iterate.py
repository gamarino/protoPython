# range_iterate.py - Benchmark: iterate over range(N) with N=100000
N = 100000


def main():
    n = 0
    for i in range(N):
        n += 1
    return n


if __name__ == "__main__":
    main()
