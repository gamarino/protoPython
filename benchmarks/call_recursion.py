def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

def run_bench(n=25):
    return fib(n)

if __name__ == "__main__":
    import sys
    n = 25
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
    run_bench(n)
