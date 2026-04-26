"""
fannkuch — pure-Python subset of pyperformance.

The fannkuch-redux benchmark from the Computer Language Benchmarks
Game.  Stresses array reversal, integer arithmetic, and a tight
nested loop.  All operations are SmallInt-bound.

Usage:  python3 bench_fannkuch.py            # default N=8
        python3 bench_fannkuch.py 9
"""
import sys
import time


def fannkuch(n):
    perm = list(range(n))
    perm1 = list(range(n))
    count = [0] * n
    max_flips = 0
    r = n
    checksum = 0
    sign = 1
    permcount = 0

    while True:
        # Copy permutation
        i = 0
        while i < n:
            perm[i] = perm1[i]
            i += 1

        # Count flips
        flips = 0
        first = perm[0]
        while first != 0:
            # Reverse perm[0..first]
            i = 0
            j = first
            while i < j:
                t = perm[i]
                perm[i] = perm[j]
                perm[j] = t
                i += 1
                j -= 1
            flips += 1
            first = perm[0]

        if flips > max_flips:
            max_flips = flips
        if sign > 0:
            checksum += flips
        else:
            checksum -= flips
        sign = -sign
        permcount += 1

        # Generate next permutation (this is the expensive Heap's-style
        # rotation that uses the count array as carry digits).
        while r > 1:
            count[r - 1] = r
            r -= 1

        while True:
            if r == n:
                return checksum, max_flips
            perm0 = perm1[0]
            i = 0
            while i < r:
                perm1[i] = perm1[i + 1]
                i += 1
            perm1[r] = perm0
            count[r] -= 1
            if count[r] > 0:
                break
            r += 1


def main():
    n = 8
    if len(sys.argv) > 1:
        n = int(sys.argv[1])

    fannkuch(n)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        cs, mf = fannkuch(n)
        times.append((time.perf_counter() - t0) * 1000)
    times.sort()
    print("fannkuch n=%d  min=%.1fms  median=%.1fms  checksum=%d max_flips=%d" %
          (n, times[0], times[2], cs, mf))


if __name__ == "__main__":
    main()
