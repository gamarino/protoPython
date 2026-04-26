"""
nqueens — pure-Python subset of pyperformance.

Counts solutions to the N-Queens problem by recursive backtracking.
Stresses: function calls, list mutation, integer comparison, recursion.
No external deps; should run identically on CPython and protoPython.

Usage:  python3 bench_nqueens.py            # default N=8
        python3 bench_nqueens.py 9          # N=9 (heavier)
"""
import sys
import time


def queens_count(row, n, cols):
    if row == n:
        return 1
    count = 0
    col = 0
    while col < n:
        ok = True
        r = 0
        while r < row:
            cc = cols[r]
            if cc == col or cc - col == row - r or col - cc == row - r:
                ok = False
                break
            r += 1
        if ok:
            cols[row] = col
            count += queens_count(row + 1, n, cols)
        col += 1
    return count


def main():
    n = 8
    if len(sys.argv) > 1:
        n = int(sys.argv[1])

    expected = {1: 1, 2: 0, 3: 0, 4: 2, 5: 10, 6: 4, 7: 40, 8: 92, 9: 352, 10: 724}

    cols = [0] * n
    queens_count(0, n, cols)  # warmup
    times = []
    for _ in range(5):
        for k in range(n):
            cols[k] = 0
        t0 = time.perf_counter()
        result = queens_count(0, n, cols)
        times.append((time.perf_counter() - t0) * 1000)
    if n in expected:
        assert result == expected[n], "wrong count for n=%d: got %d expected %d" % (n, result, expected[n])
    times.sort()
    print("nqueens n=%d  min=%.1fms  median=%.1fms  count=%d" % (n, times[0], times[2], result))


if __name__ == "__main__":
    main()
