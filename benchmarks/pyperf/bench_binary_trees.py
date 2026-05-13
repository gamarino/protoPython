"""
Binary Trees — OOP object creation and method dispatch benchmark.

Adapted from the Computer Language Benchmarks Game binary-trees task.
Stresses: class instantiation, attribute reads (LOAD_ATTR), recursive
method calls, conditional branching.  No list mutation in the hot path —
object creation and attribute access are the dominant costs.

Fair across implementations: both CPython and protoPython create real
heap objects with .left/.right attributes.  The benchmark exercises the
object model directly; no CPython-specific optimisations (list C-arrays,
specialising adaptive caches for builtins) distort the comparison.

Usage:  python3 bench_binary_trees.py      # default depth=14
        python3 bench_binary_trees.py 12   # lighter
"""
import sys
import time


class Node:
    def __init__(self, left=None, right=None):
        self.left = left
        self.right = right

    def check(self):
        left = self.left
        if left is None:
            return 1
        return 1 + left.check() + self.right.check()


def make(depth):
    if depth == 0:
        return Node()
    d = depth - 1
    return Node(make(d), make(d))


def workload(max_depth):
    min_depth = 4
    stretch_depth = max_depth + 1
    stretch = make(stretch_depth)
    total = stretch.check()

    long_lived = make(max_depth)

    depth = min_depth
    while depth <= max_depth:
        iterations = 1 << (max_depth - depth + min_depth)
        check = 0
        for _ in range(iterations):
            check += make(depth).check()
        total += check
        depth += 2

    total += long_lived.check()
    return total


def main():
    max_depth = 7  # default tuned so all three modes finish per-script
                   # wall time under the runner's 240 s timeout.
                   # protopyc currently does ~2 s/iter at depth=7 (12 s
                   # for the 6-iter script); raising to 8 would push the
                   # protopyc column to ~60 s/script.
    if len(sys.argv) > 1:
        max_depth = int(sys.argv[1])
    if max_depth < 4:
        max_depth = 4

    workload(max_depth)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        result = workload(max_depth)
        times.append((time.perf_counter() - t0) * 1000)

    times.sort()
    print("binary_trees depth=%d  min=%.1fms  median=%.1fms  total=%d" %
          (max_depth, times[0], times[2], result))


if __name__ == "__main__":
    main()
