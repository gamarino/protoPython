"""
Sieve of Eratosthenes — pure Python, classic real-code benchmark.

Counts primes up to N using a boolean sieve.  Stresses: list creation,
list subscript read/write, integer arithmetic, two nested while loops.
"""
import sys
import time


def primes_below(n):
    sieve = [True] * n
    sieve[0] = False
    if n >= 2:
        sieve[1] = False
    i = 2
    while i * i < n:
        if sieve[i]:
            j = i * i
            while j < n:
                sieve[j] = False
                j += i
        i += 1
    count = 0
    k = 0
    while k < n:
        if sieve[k]:
            count += 1
        k += 1
    return count


def main():
    # Default tuned so all three modes finish within a reasonable budget.
    # protoPython's ProtoList write is O(log N) (AVL tree), so larger N
    # blows the protopy/protopyc columns up faster than CPython's
    # amortised O(1) C-array.
    n = 5000
    if len(sys.argv) > 1:
        n = int(sys.argv[1])

    primes_below(n)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        result = primes_below(n)
        times.append((time.perf_counter() - t0) * 1000)
    times.sort()
    print("sieve n=%d  min=%.1fms  median=%.1fms  primes=%d" % (n, times[0], times[2], result))


if __name__ == "__main__":
    main()
