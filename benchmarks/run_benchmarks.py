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

# Path to add to sys.path for --script runs. Use relative path so protopy resolves scripts
# without hanging (absolute path to script dir can cause extreme slowdown in some builds).
PATH_ARG = "benchmarks" if SCRIPT_DIR.name == "benchmarks" and SCRIPT_DIR.parent == PROJECT_ROOT else str(SCRIPT_DIR)


def median(lst):
    s = sorted(lst)
    n = len(s)
    if n == 0:
        return 0.0
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def run_cmd(cmd, cwd=None, timeout=60, stderr_file=None, env=None, verbose=False):
    """Run command; return (elapsed_ms, returncode, timed_out).
    Use DEVNULL for stdout/stderr when not tracing to avoid pipe deadlock if child writes."""
    start = time.perf_counter()
    stderr_handle = None
    try:
        kwargs = {
            "cwd": cwd or PROJECT_ROOT,
            "text": True,
            "timeout": timeout,
        }
        if env is not None:
            kwargs["env"] = {**os.environ, **env}
        if stderr_file:
            stderr_handle = open(stderr_file, "a")
            kwargs["stdout"] = subprocess.DEVNULL
            kwargs["stderr"] = stderr_handle
        else:
            kwargs["stdout"] = subprocess.DEVNULL
            kwargs["stderr"] = subprocess.DEVNULL
        r = subprocess.run(cmd, **kwargs)
        elapsed_ms = (time.perf_counter() - start) * 1000
        if verbose:
            print(f"      {elapsed_ms:.0f}ms exit={r.returncode}")
        return elapsed_ms, r.returncode, False
    except subprocess.TimeoutExpired:
        elapsed_ms = (time.perf_counter() - start) * 1000
        if verbose:
            print(f"      TIMEOUT {elapsed_ms:.0f}ms")
        return elapsed_ms, -1, True
    finally:
        if stderr_handle is not None:
            try:
                stderr_handle.close()
            except Exception:
                pass


def bench_startup_empty(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Time to import minimal module (abc)."""
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--module", "abc"], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, "-c", "import abc"], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    startup_empty protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--module", "abc"], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    startup_empty cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, "-c", "import abc"], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def bench_int_sum_loop(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Time to run sum(range(N)) with N=100000."""
    script = SCRIPT_DIR / "int_sum_loop.py"
    if not script.exists():
        return None, None
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    int_sum_loop protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    int_sum_loop cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def bench_list_append_loop(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Time to run list append loop with N=10000."""
    script = SCRIPT_DIR / "list_append_loop.py"
    if not script.exists():
        return None, None
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    list_append_loop protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    list_append_loop cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def bench_str_concat_loop(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Time to run string concat loop with N=10000."""
    script = SCRIPT_DIR / "str_concat_loop.py"
    if not script.exists():
        return None, None
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    str_concat_loop protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    str_concat_loop cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def bench_range_iterate(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Time to iterate over range(N) with N=100000."""
    script = SCRIPT_DIR / "range_iterate.py"
    if not script.exists():
        return None, None
    script_str = str(script.resolve())
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    range_iterate protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    range_iterate cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def bench_multithreaded_cpu(protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """CPU-bound work: 4 chunks. CPython uses 4 threads (GIL serializes). protoPython uses SINGLE_THREAD=1 (same work in main)."""
    script = SCRIPT_DIR / "multithreaded_cpu.py"
    if not script.exists():
        return None, None
    script_str = str(script.resolve())
    protopy_env = {"SINGLE_THREAD": "1"}
    times_protopy = []
    times_cpython = []
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, env=protopy_env, verbose=verbose)
        run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
    for i in range(N_RUNS):
        if verbose:
            print(f"    multithread_cpu protopy run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_str], timeout=timeout, stderr_file=trace_file, env=protopy_env, verbose=verbose)
        if not to:
            times_protopy.append(t)
        if verbose:
            print(f"    multithread_cpu cpython run {i+1}/{N_RUNS}:", end="")
        t, _, to = run_cmd([cpython_bin, script_str], timeout=timeout, verbose=verbose)
        if not to:
            times_cpython.append(t)
    return median(times_protopy) if times_protopy else None, median(times_cpython) if times_cpython else None


def geometric_mean(ratios):
    if not ratios:
        return 0.0
    return math.exp(sum(math.log(r) if r > 0 else 0 for r in ratios) / len(ratios))


def format_report(results):
    lines = [
        "```",
        "┌─────────────────────────────────────────────────────────────────────┐",
        "│ Performance: protoPython vs CPython 3.14                            │",
        f"│ (median of {N_RUNS} runs, timeouts excluded)                             │",
        f"│ {datetime.now(timezone.utc).strftime('%Y-%m-%d')} {platform.system()} {platform.machine()}                                │",
        "├─────────────────────────────────────────────────────────────────────┤",
        "│ Benchmark              │ protopy (ms) │ cpython (ms) │ Ratio        │",
        "├────────────────────────┼──────────────┼──────────────┼──────────────┤",
    ]
    ratios = []
    for name, (tp, tc) in results.items():
        if tp is None:
            tp_str = "   TIMEOUT"
        else:
            tp_str = f"{tp:>10.2f}  "
        if tc is None:
            tc_str = "   TIMEOUT"
        else:
            tc_str = f"{tc:>10.2f}  "
        if tp is not None and tc is not None and tc > 0:
            ratio = tp / tc
            ratios.append(ratio)
            label = "slower" if ratio >= 1 else "faster"
            ratio_str = f"{ratio:.2f}x {label:<5}"
        else:
            ratio_str = "timeout"
        lines.append(f"│ {name:<22} │ {tp_str} │ {tc_str} │ {ratio_str:<12} │")
    gm = geometric_mean(ratios) if ratios else None
    lines.append("├────────────────────────┼──────────────┼──────────────┼──────────────┤")
    gm_str = f"~{gm:.2f}x" if gm is not None else "N/A"
    lines.append(f"│ Geometric mean         │              │              │ {gm_str:<12} │")
    lines.append("└─────────────────────────────────────────────────────────────────────┘")
    lines.append("")
    lines.append("Legend: Ratio = protopy/cpython. Lower is better. TIMEOUT = all runs hit timeout.")
    lines.append("```")
    return "\n".join(lines)


def run_cpython_only_bench(cpython_bin, warmup_runs, n_runs, run_fn):
    """Run only the CPython side of a benchmark; returns (0.0, median_cpython_ms)."""
    script_dir = SCRIPT_DIR
    def run_one(cmd, timeout=60):
        t, _, to = run_cmd(cmd, timeout=timeout)
        return (t, to)
    if run_fn == "startup_empty":
        for _ in range(warmup_runs):
            run_one([cpython_bin, "-c", "import abc"])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, "-c", "import abc"])
            if not to:
                times.append(t)
    elif run_fn == "int_sum_loop":
        script = script_dir / "int_sum_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_one([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, script_str])
            if not to:
                times.append(t)
    elif run_fn == "list_append_loop":
        script = script_dir / "list_append_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_one([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, script_str])
            if not to:
                times.append(t)
    elif run_fn == "str_concat_loop":
        script = script_dir / "str_concat_loop.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_one([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, script_str])
            if not to:
                times.append(t)
    elif run_fn == "range_iterate":
        script = script_dir / "range_iterate.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_one([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, script_str])
            if not to:
                times.append(t)
    elif run_fn == "multithreaded_cpu":
        script = script_dir / "multithreaded_cpu.py"
        script_str = str(script.resolve())
        for _ in range(warmup_runs):
            run_one([cpython_bin, script_str])
        times = []
        for _ in range(n_runs):
            t, to = run_one([cpython_bin, script_str])
            if not to:
                times.append(t)
    else:
        return 0.0, 0.0
    return 0.0, median(times) if times else None


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
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Print each run's elapsed time and TIMEOUT events.",
    )
    parser.add_argument(
        "--quick", "-q",
        action="store_true",
        help="Use N_RUNS=2, WARMUP_RUNS=1 for faster iteration.",
    )
    args = parser.parse_args()

    global N_RUNS, WARMUP_RUNS
    if args.quick:
        N_RUNS = 2
        WARMUP_RUNS = 1

    cpython_bin = os.environ.get("CPYTHON_BIN", "python3")

    if args.cpython_only:
        results = {}
        for name, key in [
            ("startup_empty", "startup_empty"),
            ("int_sum_loop", "int_sum_loop"),
            ("list_append_loop", "list_append_loop"),
            ("str_concat_loop", "str_concat_loop"),
            ("range_iterate", "range_iterate"),
            ("multithread_cpu", "multithreaded_cpu"),
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
    verbose = args.verbose
    print(f"Benchmark: protopy={protopy_bin} timeout={timeout}s N_RUNS={N_RUNS} WARMUP={WARMUP_RUNS}", flush=True)
    trace_file = os.environ.get("PROTO_GC_TRACE_FILE")
    if trace_file:
        # Clear or create so we only have this run's trace
        open(trace_file, "w").close()
    print("[startup_empty]", flush=True)
    tp, tc = bench_startup_empty(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["startup_empty"] = (tp, tc)
    print("[int_sum_loop]", flush=True)
    tp, tc = bench_int_sum_loop(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["int_sum_loop"] = (tp, tc)
    print("[list_append_loop]", flush=True)
    tp, tc = bench_list_append_loop(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["list_append_loop"] = (tp, tc)
    print("[str_concat_loop]", flush=True)
    tp, tc = bench_str_concat_loop(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["str_concat_loop"] = (tp, tc)
    if verbose:
        print("[range_iterate]")
    tp, tc = bench_range_iterate(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["range_iterate"] = (tp, tc)
    print("[multithread_cpu]", flush=True)
    tp, tc = bench_multithreaded_cpu(protopy_bin, cpython_bin, timeout, trace_file, verbose=verbose)
    results["multithread_cpu"] = (tp, tc)

    for name, (tp, tc) in results.items():
        if tp is None:
            tp_s = "TIMEOUT"
        else:
            tp_s = f"{tp:.2f}ms"
        if tc is None:
            tc_s = "TIMEOUT"
        else:
            tc_s = f"{tc:.2f}ms"
        if tp is not None and tc is not None and tc > 0:
            ratio = tp / tc
            label = "slower" if ratio >= 1 else "faster"
            print(f"{name}: protopy={tp_s} cpython={tc_s} ratio={ratio:.2f}x {label}")
        else:
            print(f"{name}: protopy={tp_s} cpython={tc_s}")

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
