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

Usage:
    python3 run_pyperf_subset.py /path/to/protopy
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent.parent
TIMEOUT = 60

# (label, script, args, expect_to_complete_under_timeout?)
# args is the CLI to pass to the script; pick sizes the realistic-but-not-overwhelming.
BENCHES = [
    ("nqueens(8)",        "bench_nqueens.py",       ["8"],      True),
    ("sieve(10000)",      "bench_sieve.py",         ["10000"],  True),
    ("richards_lite",     "bench_richards_lite.py", [],         True),
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
            return None, r.stderr.strip()[:120]
        # Parse "min=NNN.Nms" from stdout.
        m = re.search(r"min=([\d.]+)ms", r.stdout)
        if not m:
            return None, "no min= in output: %s" % r.stdout.strip()[:120]
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
    print("Pyperformance pure-Python subset — internal timing (no startup overhead)")
    print("=" * 88)
    print("%-22s %15s %15s %12s" % ("Benchmark", "protopy (ms)", "cpython (ms)", "Ratio"))
    print("-" * 88)

    import math
    ratios = []
    for label, script, args, _ in BENCHES:
        p_ms, p_err = run_bench(protopy, script, args)
        c_ms, c_err = run_bench(cpython, script, args)
        if p_ms is None:
            print("%-22s %15s %15s %12s" %
                  (label, p_err, "%.1f" % c_ms if c_ms else (c_err or "?"), "n/a"))
            continue
        if c_ms is None:
            print("%-22s %15.1f %15s %12s" % (label, p_ms, c_err or "?", "n/a"))
            continue
        ratio = p_ms / c_ms
        ratios.append(ratio)
        print("%-22s %15.1f %15.1f %12s" %
              (label, p_ms, c_ms, "%.1f×" % ratio))

    print("-" * 88)
    if ratios:
        gm = math.exp(sum(math.log(r) for r in ratios) / len(ratios))
        print("Geomean ratio (vs CPython 3.14): %.1f×" % gm)
    print()
    print("Note: these are pure-Python pyperformance-style benchmarks.  Tight")
    print("integer microbenchmarks (e.g. `s += i`) dramatically understate the")
    print("real gap because they exercise only the SmallInt fast path.  This")
    print("subset stresses list subscript, method dispatch, and class")
    print("instantiation — the dominant costs in real Python code.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
