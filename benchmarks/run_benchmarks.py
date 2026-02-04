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
    try:
        r = subprocess.run(
            cmd,
            cwd=cwd or PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        elapsed_ms = (time.perf_counter() - start) * 1000
        return elapsed_ms, r.returncode
    except subprocess.TimeoutExpired:
        elapsed_ms = (time.perf_counter() - start) * 1000
        return elapsed_ms, -1


def bench_startup_empty(protopy_bin, cpython_bin, timeout=60):
    """Time to import minimal module (abc)."""
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--module", "abc"], timeout=timeout)
        run_cmd([cpython_bin, "-c", "import abc"], timeout=timeout)
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--module", "abc"], timeout=timeout)
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, "-c", "import abc"], timeout=timeout)
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def bench_int_sum_loop(protopy_bin, cpython_bin, timeout=60):
    """Time to run sum(range(N)) with N=100000."""
    script = SCRIPT_DIR / "int_sum_loop.py"
    if not script.exists():
        return 0.0, 0.0
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        run_cmd([cpython_bin, script_str], timeout=timeout)
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, script_str], timeout=timeout)
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def bench_list_append_loop(protopy_bin, cpython_bin, timeout=60):
    """Time to run list append loop with N=10000."""
    script = SCRIPT_DIR / "list_append_loop.py"
    if not script.exists():
        return 0.0, 0.0
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        run_cmd([cpython_bin, script_str], timeout=timeout)
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, script_str], timeout=timeout)
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def bench_str_concat_loop(protopy_bin, cpython_bin, timeout=60):
    """Time to run string concat loop with N=10000."""
    script = SCRIPT_DIR / "str_concat_loop.py"
    if not script.exists():
        return 0.0, 0.0
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        run_cmd([cpython_bin, script_str], timeout=timeout)
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, script_str], timeout=timeout)
        times_cpython.append(t)
    return median(times_protopy), median(times_cpython)


def bench_range_iterate(protopy_bin, cpython_bin, timeout=60):
    """Time to iterate over range(N) with N=100000."""
    script = SCRIPT_DIR / "range_iterate.py"
    if not script.exists():
        return 0.0, 0.0
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        run_cmd([cpython_bin, script_str], timeout=timeout)
    for _ in range(N_RUNS):
        t, _ = run_cmd([protopy_bin, "--path", str(SCRIPT_DIR), "--script", script_str], timeout=timeout)
        times_protopy.append(t)
        t, _ = run_cmd([cpython_bin, script_str], timeout=timeout)
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


def run_cpython_only_bench(cpython_bin, warmup_runs, n_runs, run_fn):
    """Run only the CPython side of a benchmark; returns (0.0, median_cpython_ms)."""
    script_dir = SCRIPT_DIR
    if run_fn == "startup_empty":
        for _ in range(warmup_runs):
            run_cmd([cpython_bin, "-c", "import abc"])
        times = []
        for _ in range(n_runs):
            t, _ = run_cmd([cpython_bin, "-c", "import abc"])
            times.append(t)
    elif run_fn == "int_sum_loop":
        script = script_dir / "int_sum_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_cmd([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, _ = run_cmd([cpython_bin, script_str])
            times.append(t)
    elif run_fn == "list_append_loop":
        script = script_dir / "list_append_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_cmd([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, _ = run_cmd([cpython_bin, script_str])
            times.append(t)
    elif run_fn == "str_concat_loop":
        script = script_dir / "str_concat_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_cmd([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, _ = run_cmd([cpython_bin, script_str])
            times.append(t)
    elif run_fn == "range_iterate":
        script = script_dir / "range_iterate.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_cmd([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, _ = run_cmd([cpython_bin, script_str])
            times.append(t)
    else:
        return 0.0, 0.0
    return 0.0, median(times)


def format_report_cpython_only(results, cpython_bin):
    """Format report when only CPython was run (protopy N/A)."""
    lines = [
        "```",
        "┌─────────────────────────────────────────────────────────────────────┐",
        f"│ CPython 3.14 baseline only (protoPython N/A — startup hang)         │",
        f"│ (median of {N_RUNS} runs, N=100000 where applicable)                       │",
        f"│ {datetime.now(timezone.utc).strftime('%Y-%m-%d')} {platform.system()} {platform.machine()}                                │",
        "├─────────────────────────────────────────────────────────────────────┤",
        "│ Benchmark              │ protopy (ms) │ cpython (ms) │ Notes         │",
        "├────────────────────────┼──────────────┼──────────────┼──────────────┤",
    ]
    for name, (tp, tc) in results.items():
        lines.append(f"│ {name:<22} │       N/A    │ {tc:>10.2f}  │ baseline      │")
    lines.append("└─────────────────────────────────────────────────────────────────────┘")
    lines.append("")
    lines.append("Legend: protoPython was not measured (known startup/GC hang; see TESTING.md).")
    lines.append("```")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="protoPython benchmark harness")
    parser.add_argument(
        "--output", "-o",
        help="Write markdown report (e.g. reports/YYYY-MM-DD_protopy_vs_cpython.md)",
    )
    parser.add_argument(
        "--cpython-only",
        action="store_true",
        help="Run only CPython benchmarks (skip protoPython); use when protopy hangs.",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=60,
        metavar="SECS",
        help="Per-run timeout in seconds for each benchmark (default: 60).",
    )
    args = parser.parse_args()

    cpython_bin = os.environ.get("CPYTHON_BIN", "python3")

    if args.cpython_only:
        results = {}
        for name, key in [
            ("startup_empty", "startup_empty"),
            ("int_sum_loop", "int_sum_loop"),
            ("list_append_loop", "list_append_loop"),
            ("str_concat_loop", "str_concat_loop"),
            ("range_iterate", "range_iterate"),
        ]:
            _, tc = run_cpython_only_bench(cpython_bin, WARMUP_RUNS, N_RUNS, key)
            results[name] = (0.0, tc)
            print(f"{name}: cpython={tc:.2f}ms (protopy skipped)")
        if args.output:
            out_path = Path(args.output)
            if not out_path.is_absolute():
                out_path = REPORTS_DIR / out_path.name
            out_path.parent.mkdir(parents=True, exist_ok=True)
            report = format_report_cpython_only(results, cpython_bin)
            out_path.write_text(report, encoding="utf-8")
            print(f"Report written to {out_path}")
        return 0

    protopy_bin = os.environ.get("PROTOPY_BIN")
    if not protopy_bin or not os.path.isfile(protopy_bin):
        print("Error: PROTOPY_BIN must point to protopy executable", file=sys.stderr)
        sys.exit(2)
    if not os.access(protopy_bin, os.X_OK):
        print("Error: protopy binary not executable", file=sys.stderr)
        sys.exit(2)

    results = {}
    timeout = args.timeout
    tp, tc = bench_startup_empty(protopy_bin, cpython_bin, timeout)
    results["startup_empty"] = (tp, tc)
    tp, tc = bench_int_sum_loop(protopy_bin, cpython_bin, timeout)
    results["int_sum_loop"] = (tp, tc)
    tp, tc = bench_list_append_loop(protopy_bin, cpython_bin, timeout)
    results["list_append_loop"] = (tp, tc)
    tp, tc = bench_str_concat_loop(protopy_bin, cpython_bin, timeout)
    results["str_concat_loop"] = (tp, tc)
    tp, tc = bench_range_iterate(protopy_bin, cpython_bin, timeout)
    results["range_iterate"] = (tp, tc)

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
