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
    """Run command; return (elapsed_ms, returncode, timed_out, peak_rss_kb)."""
    # Use /usr/bin/time -f "%M" to get peak RSS in KB
    time_cmd = ["/usr/bin/time", "-f", "%M"] + cmd
    start = time.perf_counter()
    stderr_handle = None
    is_protopy = cmd and "protopy" in os.path.basename(cmd[0])
    try:
        kwargs = {
            "cwd": cwd or PROJECT_ROOT,
            "text": True,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.PIPE,
        }
        if env is not None:
            kwargs["env"] = {**os.environ, **env}
        
        p = subprocess.Popen(time_cmd, **kwargs)
        try:
            stdout_data, stderr_data = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()
            if verbose: print(" TIMEOUT", end="")
            return (time.perf_counter() - start) * 1000, -1, True, 0

        elapsed_ms = (time.perf_counter() - start) * 1000
        
        rss = 0
        if stderr_data:
            lines = stderr_data.strip().split("\n")
            if lines:
                try:
                    rss = int(lines[-1].strip())
                except ValueError:
                    if verbose: print(f" (RSS parse fail: {lines[-1]})", end="")
                    pass

        if verbose:
            print(f" {elapsed_ms:.0f}ms (exit={p.returncode}, rss={rss}KB)", end="")
        return elapsed_ms, p.returncode, False, rss
    except Exception as e:
        if verbose:
            print(f"      ERROR: {e}")
        return 0, -1, False, 0


def _script_paths(script_path):
    """Return (path_for_protopy, path_for_cpython). Use relative for protopy to avoid slowdown."""
    script = script_path if isinstance(script_path, Path) else Path(script_path)
    try:
        rel = str(script.relative_to(PROJECT_ROOT))
    except ValueError:
        rel = str(script.resolve())
    return rel, str(script.resolve())


def bench_generic(name, script_name, protopy_bin, cpython_bin, timeout=60, trace_file=None, verbose=False):
    """Generic benchmark runner."""
    script = SCRIPT_DIR / script_name
    if not script.exists():
        return None, None, None, None
    script_protopy, script_cpy = _script_paths(script)
    times_p, times_c = [], []
    rss_p, rss_c = [], []
    
    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_protopy], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_cpy], timeout=timeout, verbose=verbose)
    
    for i in range(N_RUNS):
        if verbose: print(f"    {name} protopy {i+1}/{N_RUNS}:", end="")
        tp, _, to, rp = run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_protopy], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        if not to:
            times_p.append(tp)
            rss_p.append(rp)
        
        if verbose: print(f"    {name} cpython {i+1}/{N_RUNS}:", end="")
        tc, _, to, rc = run_cmd([cpython_bin, script_cpy], timeout=timeout, verbose=verbose)
        if not to:
            times_c.append(tc)
            rss_c.append(rc)
            
    return (median(times_p) if times_p else None, median(times_c) if times_c else None,
            median(rss_p) if rss_p else None, median(rss_c) if rss_c else None)


def format_report(results):
    lines = [
        "```",
        "┌──────────────────────────────────────────────────────────────────────────────────────┐",
        "│ Performance Audit: protoPython vs CPython 3.14                                       │",
        f"│ (median of {N_RUNS} runs, timeouts excluded)                                                │",
        f"│ {datetime.now(timezone.utc).strftime('%Y-%m-%d')} {platform.system()} {platform.machine()}                                           │",
        "├────────────────────────┬──────────────┬──────────────┬──────────────┬────────────────┤",
        "│ Benchmark              │ Time P (ms)  │ Time C (ms)  │ Ratio        │ Peak RSS (P/C) │",
        "├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤",
    ]
    ratios = []
    for name, (tp, tc, rp, rc) in results.items():
        tp_str = f"{tp:>10.2f}" if tp is not None else "   TIMEOUT"
        tc_str = f"{tc:>10.2f}" if tc is not None else "   TIMEOUT"
        if tp is not None and tc is not None and tc > 0:
            ratio = tp / tc
            ratios.append(ratio)
            label = "slower" if ratio >= 1 else "faster"
            ratio_str = f"{ratio:.2f}x {label:<6}"
        else:
            ratio_str = "timeout"
        
        mem_str = f"{rp/1024:>5.1f}/{rc/1024:>5.1f}MB" if rp and rc else "N/A"
        lines.append(f"│ {name:<22} │ {tp_str}   │ {tc_str}   │ {ratio_str:<12} │ {mem_str:<14} │")
    
    gm = math.exp(sum(math.log(r) for r in ratios) / len(ratios)) if ratios else 0
    lines.append("├────────────────────────┼──────────────┼──────────────┼──────────────┼────────────────┤")
    lines.append(f"│ Geomean Time Ratio     │              │              │ {gm:>5.2f}x        │                │")
    lines.append("└──────────────────────────────────────────────────────────────────────────────────────┘")
    lines.append("```")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", "-o")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--quick", "-q", action="store_true")
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    global N_RUNS, WARMUP_RUNS
    if args.quick: N_RUNS, WARMUP_RUNS = 2, 1
    
    protopy_bin = os.environ.get("PROTOPY_BIN")
    if not protopy_bin:
        print("Set PROTOPY_BIN environment variable.")
        return 1
    cpython_bin = os.environ.get("CPYTHON_BIN", "python3")
    
    benchmarks = [
        ("startup_empty", "abc", True), # specialized
        ("int_sum_loop", "int_sum_loop.py", False),
        ("list_append_loop", "list_append_loop.py", False),
        ("str_concat_loop", "str_concat_loop.py", False),
        ("range_iterate", "range_iterate.py", False),
        ("multithread_cpu", "multithreaded_cpu.py", False),
        ("attr_lookup", "attr_lookup.py", False),
        ("call_recursion", "call_recursion.py", False),
        ("memory_pressure", "memory_pressure.py", False),
    ]

    results = {}
    for name, script, is_mod in benchmarks:
        print(f"Running {name}...")
        if name == "startup_empty":
            tp, tc, rp, rc = bench_generic("startup", script, protopy_bin, cpython_bin, args.timeout, verbose=args.verbose) # Won't work as it expects .py
            # Fix for startup_empty which uses --module
            times_p, times_c, rss_p, rss_c = [], [], [], []
            for _ in range(WARMUP_RUNS):
                run_cmd([protopy_bin, "--module", "abc"])
                run_cmd([cpython_bin, "-c", "import abc"])
            for _ in range(N_RUNS):
                tp, _, _, rp = run_cmd([protopy_bin, "--module", "abc"])
                times_p.append(tp); rss_p.append(rp)
                tc, _, _, rc = run_cmd([cpython_bin, "-c", "import abc"])
                times_c.append(tc); rss_c.append(rc)
            results[name] = (median(times_p), median(times_c), median(rss_p), median(rss_c))
        else:
            results[name] = bench_generic(name, script, protopy_bin, cpython_bin, args.timeout, verbose=args.verbose)

    report = format_report(results)
    print(report)
    if args.output:
        Path(args.output).write_text(report)
    return 0

if __name__ == "__main__":
    sys.exit(main())


if __name__ == "__main__":
    sys.exit(main())
