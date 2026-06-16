#!/usr/bin/env python3
"""4-way benchmark runner: protopy / protopyc / CPython GIL / CPython no-GIL.

Per bench, runs every binary interleaved (warmup x2 each then N_RUNS x4
interleaved rounds) so any per-bench system load shifts hit all four
binaries equally.  Avoids the "system shifted between runs" artefact
of running run_benchmarks.py twice with different CPython baselines.

Usage:
  PROTOPY_BIN=...  PROTOPYC_BIN=...  RUN_MODULE_BIN=...
  CPYTHON_BIN=...  CPYTHONT_BIN=...
  python3 run_4way_interleaved.py [--output report.md]
"""
import argparse
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Reuse helpers from the main harness.
sys.path.insert(0, str(Path(__file__).parent))
from run_benchmarks import (  # noqa: E402
    SCRIPT_DIR, PROJECT_ROOT, run_cmd, compile_protopyc, validate_run,
)
BENCH_DIR = SCRIPT_DIR

N_RUNS = 5
WARMUP_RUNS = 2


def median_ms(times):
    times = [t for t in times if t and t > 0]
    return statistics.median(times) if times else None


def run_4way(name, script_rel, args_for_script,
             protopy_bin, protopyc_so, run_module_bin,
             cpython_bin, cpythont_bin, lib_env, timeout):
    """Measure one bench with all four binaries interleaved."""
    abs_script = (BENCH_DIR / script_rel).resolve() if script_rel else None
    rel_script = str(abs_script.relative_to(PROJECT_ROOT)) if abs_script else None
    py_script  = str(abs_script) if abs_script else None
    protopy_cmd  = [protopy_bin, rel_script] + args_for_script if rel_script else None
    cpython_cmd  = [cpython_bin, py_script]  + args_for_script if py_script else None
    cpythont_cmd = [cpythont_bin, py_script] + args_for_script if py_script else None
    protopyc_cmd = ([run_module_bin, str(protopyc_so)] + args_for_script
                    if protopyc_so and run_module_bin else None)

    bins = [("protopy", protopy_cmd), ("protopyc", protopyc_cmd),
            ("cpython", cpython_cmd), ("cpythont", cpythont_cmd)]
    bins = [(label, cmd) for label, cmd in bins if cmd]

    # Warmup pass: every binary gets WARMUP_RUNS warmups before any measurement.
    for label, cmd in bins:
        for _ in range(WARMUP_RUNS):
            run_cmd(cmd, env=lib_env, timeout=timeout)

    # Measurement: N_RUNS interleaved rounds.  Each round visits all four
    # binaries so a transient load spike during round k hits every column.
    # validate_run drops silent failures (non-zero exit, empty stdout when
    # a pattern is required, etc.) instead of letting them poison medians.
    times = {label: [] for label, _ in bins}
    for _ in range(N_RUNS):
        for label, cmd in bins:
            r = run_cmd(cmd, env=lib_env, timeout=timeout)
            if not validate_run(name, r, label):
                continue
            times[label].append(r["elapsed_ms"])

    return {label: median_ms(t) for label, t in times.items()}


def startup_4way(protopy_bin, cpython_bin, cpythont_bin, timeout):
    """startup_empty has no source file — measure interpreter init only."""
    bins = [
        ("protopy",  [protopy_bin, "--module", "abc"]),
        ("cpython",  [cpython_bin, "-c", "import abc"]),
        ("cpythont", [cpythont_bin, "-c", "import abc"]),
    ]
    for label, cmd in bins:
        for _ in range(WARMUP_RUNS):
            run_cmd(cmd, timeout=timeout)
    times = {label: [] for label, _ in bins}
    for _ in range(N_RUNS):
        for label, cmd in bins:
            r = run_cmd(cmd, timeout=timeout)
            if not validate_run("startup_empty", r, label):
                continue
            times[label].append(r["elapsed_ms"])
    return {label: median_ms(t) for label, t in times.items()}


def fmt_ratio(ms, base_ms):
    if not ms or not base_ms:
        return "    N/A   "
    r = ms / base_ms
    if r < 1.0:
        return f"  {r:4.2f}x F "
    return f"  {r:4.2f}x S "


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", "-o")
    p.add_argument("--timeout", type=int, default=120)
    args = p.parse_args()

    protopy_bin  = os.environ["PROTOPY_BIN"]
    protopyc_bin = os.environ.get("PROTOPYC_BIN")
    run_module_bin = os.environ.get("RUN_MODULE_BIN")
    cpython_bin  = os.environ["CPYTHON_BIN"]
    cpythont_bin = os.environ["CPYTHONT_BIN"]
    extra_ld = os.environ.get("LD_LIBRARY_PATH_BENCH", "")
    if not extra_ld:
        build_dir = Path(protopy_bin).resolve().parent.parent.parent
        extra_ld = ":".join([str(build_dir / "src" / "library"),
                             str(build_dir / "protoCore")])
    lib_env = {"LD_LIBRARY_PATH":
               extra_ld + (":" + os.environ.get("LD_LIBRARY_PATH", ""))}

    benchmarks = [
        # name, script, has_protopyc
        ("startup_empty",        None,                          False),
        ("int_sum_loop",         "int_sum_loop.py",             True),
        ("list_append_loop",     "list_append_loop.py",         True),
        ("str_concat_loop",      "str_concat_loop.py",          True),
        ("range_iterate",        "range_iterate.py",            True),
        ("multithread_cpu",      "multithreaded_cpu.py",        True),
        ("attr_lookup",          "attr_lookup.py",              True),
        ("call_recursion",       "call_recursion.py",           True),
        ("memory_pressure",      "memory_pressure.py",          True),
        ("pyperf_fib",           "pyperf/bench_fib.py",         True),
        ("pyperf_binary_trees",  "pyperf/bench_binary_trees.py", True),
        ("pyperf_nqueens",       "pyperf/bench_nqueens.py",     True),
        ("pyperf_richards_lite", "pyperf/bench_richards_lite.py", True),
        ("pyperf_sieve",         "pyperf/bench_sieve.py",       True),
    ]

    results = {}
    tmp_root = Path(tempfile.mkdtemp(prefix="bench4_"))
    for name, script_rel, allow_pyc in benchmarks:
        print(f"Running {name}...", flush=True)
        if name == "startup_empty":
            r = startup_4way(protopy_bin, cpython_bin, cpythont_bin, args.timeout)
            r["protopyc"] = None
        else:
            protopyc_so = None
            if allow_pyc and protopyc_bin and run_module_bin:
                so = compile_protopyc(
                    BENCH_DIR / script_rel, protopyc_bin,
                    tmp_root / name, lib_env, verbose=False)
                protopyc_so = so
            r = run_4way(name, script_rel, [],
                         protopy_bin, protopyc_so, run_module_bin,
                         cpython_bin, cpythont_bin, lib_env, args.timeout)
        results[name] = r

    # Report.
    lines = []
    lines.append("# protoPython 4-way interleaved benchmark (sprint-9)\n")
    lines.append("Each bench runs the four binaries interleaved (warmup x2 each, "
                 "then N=5 interleaved rounds), so any system load shift hits "
                 "every column equally — directly comparable wall-clocks.\n")
    lines.append("Columns:\n")
    lines.append("* **CPython** — uv-installed cpython-3.14.6-linux (Clang 22.1.3), **GIL ON**\n")
    lines.append("* **CPython-t** — uv-installed cpython-3.14.6+freethreaded (Clang 22.1.3), **GIL OFF**\n")
    lines.append("* **protopy** — bytecode interpreter (no GIL by construction)\n")
    lines.append("* **protopyc** — AOT-compiled C++ via `protopyc --build-so` (no GIL)\n\n")

    # Build a table.  All ratios are vs CPython-t (no-GIL = the honest
    # GIL-free baseline).  cp/cpt < 1.0 = CPython with GIL is faster
    # single-thread (the lock-cost saving); cp/cpt > 1.0 in multi-thread
    # = GIL serialisation cost.
    lines.append("| Benchmark              | CPython-t (ms, base) | CPython (ms) | cp/cpt | protopy (ms) | py/cpt | protopyc (ms) | pc/cpt |")
    lines.append("|------------------------|---------------------:|-------------:|-------:|-------------:|-------:|--------------:|-------:|")
    excluded = {"memory_pressure"}

    cp_over_cpt = []
    py_over_cpt = []
    pc_over_cpt = []

    def fmt_n(x): return f"{x:8.2f}" if x is not None else "   N/A  "

    for name, r in results.items():
        cp  = r.get("cpython")
        cpt = r.get("cpythont")
        py  = r.get("protopy")
        pyc = r.get("protopyc")

        cp_cpt  = (cp/cpt)  if cpt and cp  else None
        py_cpt  = (py/cpt)  if cpt and py  else None
        pc_cpt  = (pyc/cpt) if cpt and pyc else None

        tag = " [INFO]" if name in excluded else ""

        lines.append(
            f"| {name+tag:22s} | {fmt_n(cpt)} | {fmt_n(cp)} | "
            f"{(f'{cp_cpt:5.2f}x' if cp_cpt else '  N/A ')} | "
            f"{fmt_n(py)} | "
            f"{(f'{py_cpt:5.2f}x' if py_cpt else '  N/A ')} | "
            f"{fmt_n(pyc)} | "
            f"{(f'{pc_cpt:5.2f}x' if pc_cpt else '  N/A ')} |"
        )

        if name not in excluded:
            if cp_cpt: cp_over_cpt.append(cp_cpt)
            if py_cpt: py_over_cpt.append(py_cpt)
            if pc_cpt: pc_over_cpt.append(pc_cpt)

    def geom(xs):
        if not xs: return None
        p = 1.0
        for x in xs:
            p *= x
        return p ** (1.0 / len(xs))

    g_cp_cpt  = geom(cp_over_cpt)
    g_py_cpt  = geom(py_over_cpt)
    g_pc_cpt  = geom(pc_over_cpt)

    lines.append("")
    lines.append("## Geomeans (memory_pressure excluded) — baseline is CPython-t (no GIL)")
    lines.append(f"* CPython (GIL on) / CPython-t: **{g_cp_cpt:.2f}x** — <1.0 means GIL build is faster single-thread (the lock-cost saving)")
    lines.append(f"* protopy / CPython-t: **{g_py_cpt:.2f}x**")
    lines.append(f"* protopyc / CPython-t: **{g_pc_cpt:.2f}x**")

    report = "\n".join(lines) + "\n"
    print(report)
    if args.output:
        Path(args.output).write_text(report)

    shutil.rmtree(tmp_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
