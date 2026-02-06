def run_bench(n=50000):
    total = 0
    for i in range(n):
        try:
            if i % 2 == 0:
                raise ValueError("even")
            total += 1
        except ValueError:
            total += 1
    return total

if __name__ == "__main__":
    import sys
    n = 50000
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    run_bench(n)
