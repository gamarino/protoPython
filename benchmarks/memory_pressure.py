def run_bench(n=100000):
    # Heavy allocation/deallocation churn
    data = []
    for i in range(n):
        data.append([i] * 10)
        if len(data) > 1000:
            data.pop(0)
    return len(data)

if __name__ == "__main__":
    import sys
    n = 100000
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    run_bench(n)
