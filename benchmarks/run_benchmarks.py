#!/usr/bin/env python3
"""
Benchmark harness for protoPython vs CPython.
Runs workloads N times, records median wall time.
Usage: PROTOPY_BIN=/path/to/protopy python3 run_benchmarks.py [--output reports/YYYY-MM-DD.md]
       CPYTHON_BIN=python3.14 (optional, default python3)
"""

import argparse
import math
import os
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
REPORTS_DIR = SCRIPT_DIR / "reports"
N_RUNS = 5
WARMUP_RUNS = 2


def median(lst):
    s = sorted(lst)
    n = len(s)
    if n == 0:
        return 0.0
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def run_cmd(cmd, cwd=None, timeout=60):
    start = time.perf_counter()
    r = subprocess.run(
        cmd,
        cwd=cwd or PROJECT_ROOT,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    elapsed_ms = (time.perf_counter() - start) * 1000
    return elapsed_ms, r.returncode


def bench_startup_empty(protopy_bin, cpython_bin):
    """Time to import minimal module (abc)."""
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--module", "abc"])
        run_cmd([cpython_bin, "-c", "import abc"])
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--module", "abc"])
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, "-c", "import abc"])
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def bench_int_sum_loop(protopy_bin, cpython_bin):
    """Time to run sum(range(N)) with N=100000."""
    script = SCRIPT_DIR / "int_sum_loop.py"
    if not script.exists():
        return 0.0, 0.0
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str])
        run_cmd([cpython_bin, script_str])
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str])
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, script_str])
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def geometric_mean(ratios):
    if not ratios:
        return 0.0
    return math.exp(sum(math.log(r) if r > 0 else 0 for r in ratios) / len(ratios))


def format_report(results):
    lines = [
        "```",
        "┌─────────────────────────────────────────────────────────────────────┐",
        "│ Performance: protoPython vs CPython 3.14                            │",
        f"│ (median of {N_RUNS} runs, N=100000 where applicable)                       │",
        f"│ {datetime.now(timezone.utc).strftime('%Y-%m-%d')} {platform.system()} {platform.machine()}                                │",
        "├─────────────────────────────────────────────────────────────────────┤",
        "│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │",
        "├────────────────────────┼──────────────┼──────────────┼──────────────┤",
    ]
    ratios = []
    for name, (tp, tc) in results.items():
        ratio = tp / tc if tc > 0 else 0
        ratios.append(ratio)
        label = "slower" if ratio >= 1 else "faster"
        lines.append(f"│ {name:<22} │ {tp:>10.2f}  │ {tc:>10.2f}  │ {ratio:.2f}x {label:<5} │")
    gm = geometric_mean(ratios)
    lines.append("├────────────────────────┼──────────────┼──────────────┼──────────────┤")
    lines.append(f"│ Geometric mean         │              │              │ ~{gm:.2f}x        │")
    lines.append("└─────────────────────────────────────────────────────────────────────┘")
    lines.append("")
    lines.append("Legend: Ratio = protopy/cpython. Lower is better for protoPython.")
    lines.append("```")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="protoPython benchmark harness")
    parser.add_argument(
        "--output", "-o",
        help="Write markdown report (e.g. reports/YYYY-MM-DD_protopy_vs_cpython.md)",
    )
    args = parser.parse_args()

    protopy_bin = os.environ.get("PROTOPY_BIN")
    cpython_bin = os.environ.get("CPYTHON_BIN", "python3")
    if not protopy_bin or not os.path.isfile(protopy_bin):
        print("Error: PROTOPY_BIN must point to protopy executable", file=sys.stderr)
        sys.exit(2)
    if not os.access(protopy_bin, os.X_OK):
        print("Error: protopy binary not executable", file=sys.stderr)
        sys.exit(2)

    results = {}
    tp, tc = bench_startup_empty(protopy_bin, cpython_bin)
    results["startup_empty"] = (tp, tc)
    tp, tc = bench_int_sum_loop(protopy_bin, cpython_bin)
    results["int_sum_loop"] = (tp, tc)
    tp, tc = bench_list_append_loop(protopy_bin, cpython_bin)
    results["list_append_loop"] = (tp, tc)

    for name, (tp, tc) in results.items():
        ratio = tp / tc if tc > 0 else 0
        label = "slower" if ratio >= 1 else "faster"
        print(f"{name}: protopy={tp:.2f}ms cpython={tc:.2f}ms ratio={ratio:.2f}x {label}")

    if args.output:
        out_path = Path(args.output)
        if not out_path.is_absolute():
            out_path = REPORTS_DIR / out_path.name
        out_path.parent.mkdir(parents=True, exist_ok=True)
        report = format_report(results)
        out_path.write_text(report, encoding="utf-8")
        print(f"Report written to {out_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
