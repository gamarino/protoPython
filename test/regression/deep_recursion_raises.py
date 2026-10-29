# Recursion past what the native stack holds raises RecursionError instead
# of crashing the process.  Every Python call recurses on the native stack,
# and with the recursion limit raised (test.support.infinite_recursion sets
# 20,000; on Linux 2,000 was enough) the interpreter died of SIGSEGV.
import functools
import sys

sys.setrecursionlimit(100_000)


def down(n):
    return 0 if n == 0 else 1 + down(n - 1)


@functools.lru_cache
def fib(n):
    return n if n <= 1 else fib(n - 1) + fib(n - 2)


for f, arg in ((down, 90_000), (fib, 90_000)):
    try:
        f(arg)
    except RecursionError:
        pass

# Recovery: ordinary calls work afterwards.
sys.setrecursionlimit(1000)
assert down(500) == 500
assert fib(50) == 12586269025
print("deep_recursion_raises: ok")
