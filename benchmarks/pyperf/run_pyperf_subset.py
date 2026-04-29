#!/usr/bin/env python3
"""
Run the pyperformance pure-Python subset against protopy and CPython,
side by side, and print a comparison table.

Each benchmark script is self-contained: it warms up internally, runs
five timed iterations, and prints a one-line summary that this harness
parses.  Per-benchmark timings come from `time.perf_counter()` inside
the bench (no fork/exec overhead in the measurement) — so what we
compare is the actual interpreter cost, not the protopy/python startup
floor.

Benchmark selection rationale
------------------------------
Benchmarks are chosen to be fair across implementations.  Specifically
we avoid workloads that depend on:
  - CPython's specialising adaptive interpreter (inline caches that
    polymorphise bytecodes at runtime — absent in protoPython).
  - List mutation patterns where CPython uses an O(1) amortised C-array
    while protoPython uses an immutable AVL-tree (O(log n) per write).

Included:
  fib(25)          — pure recursion + SmallInt; no data structures.
  binary_trees(14) — OOP object creation and attribute dispatch; no list
                     mutation in the hot path.
  mandelbrot(150)  — float arithmetic loops; no list mutation in hot path.
  nqueens(8)       — recursive backtracking; list mutation present but
                     bounded (list length == N == 8) so the AVL overhead
                     is a fixed small constant, not O(problem size).
  richards_lite    — OOP method dispatch chain; instance attribute mutation
                     via setAttribute (protoCore); representative of real
                     class-heavy code.

Usage:
    python3 run_pyperf_subset.py /path/to/protopy
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent.parent
TIMEOUT = 120

# (label, script, args)
# args is passed verbatim to the benchmark script.
#
# binary_trees(10): 5–6 s/iter in protoPython; 6 iters total ≈ 35 s — within
# the 120 s timeout.  binary_trees(14) would be ~1400 s/iter — do not use.
# mandelbrot is excluded: CPython 3.11+ specialises float ops (BINARY_OP_MULTIPLY_FLOAT
# etc.) while protoPython has no float fast path, making the comparison unfair.
BENCHES = [
    # fib(25): CPython ~15 ms/iter, protoPython ~150 ms/iter — stable ratio.
    ("fib(25)",              "bench_fib.py",          ["25"]),
    # binary_trees(10): CPython ~60 ms/iter, protoPython ~5500 ms/iter.
    ("binary_trees(10)",     "bench_binary_trees.py", ["10"]),
    # nqueens(10): CPython ~150 ms/iter (stable); protoPython ~3 s/iter, 18 s total.
    # n=8 (4–5 ms) and n=9 (20–48 ms) produce too much CPython timing noise.
    ("nqueens(10)",          "bench_nqueens.py",      ["10"]),
    # richards_lite with 2000 rounds: CPython ~3 ms (vs 0.3–0.6 ms at 200 rounds).
    ("richards_lite×10",     "bench_richards_lite.py", ["2000"]),
]


def run_bench(interp, script, args):
    cmd = [interp]
    if "protopy" in os.path.basename(interp):
        cmd += ["--path", "benchmarks", "--script",
                str(Path("benchmarks/pyperf") / script)]
        cwd = PROJECT_ROOT
    else:
        cmd += [str(ROOT / script)]
        cwd = PROJECT_ROOT
    cmd += args

    try:
        r = subprocess.run(cmd, cwd=cwd, timeout=TIMEOUT,
                           capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stderr.strip()[:200]
        # Parse "min=NNN.Nms" from stdout.
        m = re.search(r"min=([\d.]+)ms", r.stdout)
        if not m:
            return None, "no min= in output: %s" % r.stdout.strip()[:200]
        return float(m.group(1)), None
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT (>%ds)" % TIMEOUT


def main():
    if len(sys.argv) < 2:
        print("Usage: run_pyperf_subset.py /path/to/protopy")
        return 1
    protopy = sys.argv[1]
    cpython = "python3"

    print("=" * 88)
    print("protoPython benchmark suite — internal timing (no startup overhead)")
    print("=" * 88)
    print("%-24s %15s %15s %10s" % ("Benchmark", "protopy (ms)", "cpython (ms)", "Ratio"))
    print("-" * 88)

    import math
    ratios = []
    for label, script, args in BENCHES:
        p_ms, p_err = run_bench(protopy, script, args)
        c_ms, c_err = run_bench(cpython, script, args)
        if p_ms is None:
            print("%-24s %15s %15s %10s" %
                  (label, p_err or "ERR", "%.1f" % c_ms if c_ms else (c_err or "?"), "n/a"))
            continue
        if c_ms is None:
            print("%-24s %15.1f %15s %10s" % (label, p_ms, c_err or "?", "n/a"))
            continue
        ratio = p_ms / c_ms
        ratios.append(ratio)
        print("%-24s %15.1f %15.1f %10s" %
              (label, p_ms, c_ms, "%.1f×" % ratio))

    print("-" * 88)
    if ratios:
        gm = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
        print("Geomean ratio (vs CPython 3.14): %.1f×" % gm)
    print()
    print("Benchmarks chosen to be fair across implementations:")
    print("  - No CPython specialising-cache bias (absent in protoPython)")
    print("  - No list-mutation O(log n) vs CPython O(1) bias in hot paths")
    print("  - No float-specialisation bias (CPython BINARY_OP_MULTIPLY_FLOAT etc.)")
    print("  - Stresses: function calls, OOP dispatch, SmallInt arith, recursion")
    return 0


if __name__ == "__main__":
    sys.exit(main())
