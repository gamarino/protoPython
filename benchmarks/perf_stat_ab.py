#!/usr/bin/env python3
"""A/B cycles and instructions of call-heavy benchmarks between protopy builds.

usage: perf_stat_ab.py label=/path/to/protopy [label=/path/to/protopy ...]

The first label is the baseline of the summary table (minimum over the rounds).
Needs Linux perf; runs under LC_ALL=C so perf prints plain decimals.

Each benchmark runs under `perf stat -r 5 -e cycles,instructions` per binary,
the binaries interleaved in two rounds. Every run's own result line is checked
against the expected computed value, so a crash or a wrong answer is never
reported as a timing.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = REPO + "/benchmarks"
# name, script, args, regex the last stdout line must match (computed result)
BENCHES = [
    ("call_recursion fib(27)", BENCH + "/call_recursion.py", ["27"], r"result=196418\b"),
    ("pyperf fib(25) x6", BENCH + "/pyperf/bench_fib.py", ["25"], r"result=75025\b"),
    ("richards_lite 200x20 x6", BENCH + "/pyperf/bench_richards_lite.py", [], r"total=57900\b"),
    ("nqueens 8 (recursion)", BENCH + "/pyperf/bench_nqueens.py", ["8"], r"count=92\b"),
    ("attr_lookup 2M", BENCH + "/attr_lookup.py", ["2000000"], r"result=12000000\b"),
]


def perf_stat(binary, script, args):
    cmd = ["perf", "stat", "-r", "5", "-x", ",", "-e", "cycles,instructions",
           binary, script] + args
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=1800, cwd=REPO, env=dict(os.environ, LC_ALL="C"))
    counters = {}
    for line in p.stderr.splitlines():
        parts = line.split(",")
        if len(parts) > 3 and parts[2].startswith(("cycles", "instructions")):
            ev = parts[2].split(":")[0]
            try:
                counters[ev] = float(parts[0])
            except ValueError:
                pass
            counters[ev + "_var"] = parts[3]
    return p.returncode, p.stdout, p.stderr, counters


def main():
    bins = [a.split("=", 1) for a in sys.argv[1:]]
    results = {}
    for name, script, args, pat in BENCHES:
        for rnd in range(2):
            for label, binary in bins:
                rc, out, err, c = perf_stat(binary, script, args)
                lines = [ln for ln in out.splitlines() if ln.strip()]
                # perf -r 5 runs it five times: five result lines.
                ok = rc == 0 and len(lines) >= 5 and all(re.search(pat, ln) for ln in lines[-5:])
                if not ok:
                    print(f"INVALID {name} {label}: rc={rc} out={lines[-2:]} err={err.strip().splitlines()[-3:]}")
                    sys.exit(1)
                results.setdefault((name, label), []).append(c)
                print(f"{name:28s} {label:8s} r{rnd} cycles={c.get('cycles',0)/1e6:10.1f}M "
                      f"(+-{c.get('cycles_var')}) instr={c.get('instructions',0)/1e6:10.1f}M  [{lines[-1].strip()}]",
                      flush=True)
    print()
    base = bins[0][0]
    print(f"| benchmark | build | cycles (M) | instructions (M) | cycles vs {base} | instructions vs {base} |")
    print("|---|---|---:|---:|---:|---:|")
    for name, *_ in BENCHES:
        bc = min(c["cycles"] for c in results[(name, base)])
        bi = min(c["instructions"] for c in results[(name, base)])
        for label, _ in bins:
            cyc = min(c["cycles"] for c in results[(name, label)])
            ins = min(c["instructions"] for c in results[(name, label)])
            print(f"| {name} | {label} | {cyc/1e6:.1f} | {ins/1e6:.1f} | {100*(cyc/bc-1):+.2f}% | {100*(ins/bi-1):+.2f}% |")


if __name__ == "__main__":
    main()
