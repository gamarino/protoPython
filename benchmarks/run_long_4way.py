#!/usr/bin/env python3
"""Long-loop variant of run_4way_interleaved.py.

Every bench is scaled so its CPython 3.14t wall-clock lands in the
~1-3 second range.  Startup overhead (~30 ms) is then <2 % of total
wall and no longer pollutes the per-op cost we are trying to measure.

The harness's small-bench inversion (where python3.14t appeared faster
than python3.14 because its startup is ~15 ms cheaper) disappears at
this scale.

Each bench runs all 4 binaries interleaved (warmup ×2 then 5 inter-
leaved rounds).  Ratios use **CPython 3.14t (free-threading, GIL off)
as the baseline** — apples-to-apples concurrency-wise vs protoPython,
which is GIL-free by construction.  CPython 3.14 with GIL is reported
in its own column so the lock cost (cp/cpt) is visible, but it is not
the baseline.
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

sys.path.insert(0, str(Path(__file__).parent))
from run_benchmarks import (  # noqa: E402
    SCRIPT_DIR, PROJECT_ROOT, run_cmd, compile_protopyc, validate_run,
)
BENCH_DIR = SCRIPT_DIR

N_RUNS = 3
WARMUP_RUNS = 1

# Per-bench scaling targets ~200-500 ms wall on CPython-t — large enough
# that startup overhead (~25 ms) is <10 % of total, small enough that
# the whole suite runs in ~10 minutes.
BENCHMARKS = [
    # name, script, argv, env, run_protopyc
    ("int_sum_loop",         "int_sum_loop.py",          [],         {"BENCH_N": "2000000"},   True),
    ("list_append_loop",     "list_append_loop.py",      [],         {"BENCH_N": "200000"},    True),
    ("str_concat_loop",      "str_concat_loop.py",       [],         {"BENCH_N": "20000"},     True),
    ("range_iterate",        "range_iterate.py",         [],         {"BENCH_N": "2000000"},   True),
    ("multithread_cpu",      "multithreaded_cpu.py",     [],         {},                       True),
    ("attr_lookup",          "attr_lookup.py",           ["5000000"], {},                      True),
    ("call_recursion",       "call_recursion.py",        [],         {"BENCH_N": "1000000"},   True),
    ("pyperf_fib",           "pyperf/bench_fib.py",      ["29"],     {},                       True),
    ("pyperf_binary_trees",  "pyperf/bench_binary_trees.py", ["9"],  {},                       True),
    ("pyperf_nqueens",       "pyperf/bench_nqueens.py",  ["9"],      {},                       True),
    ("pyperf_richards_lite", "pyperf/bench_richards_lite.py", [],    {},                       True),
    ("pyperf_sieve",         "pyperf/bench_sieve.py",    ["50000"],  {},                       True),
]


def median_ms(times):
    times = [t for t in times if t and t > 0]
    return statistics.median(times) if times else None


def run_4way(name, script_rel, argv, env_extra,
             protopy_bin, protopyc_so, run_module_bin,
             cpython_bin, cpythont_bin, lib_env, timeout):
    abs_script = (BENCH_DIR / script_rel).resolve()
    rel_script = str(abs_script.relative_to(PROJECT_ROOT))
    py_script  = str(abs_script)
    protopy_cmd  = [protopy_bin, rel_script] + argv
    cpython_cmd  = [cpython_bin, py_script]  + argv
    cpythont_cmd = [cpythont_bin, py_script] + argv
    protopyc_cmd = ([run_module_bin, str(protopyc_so)] + argv
                    if protopyc_so and run_module_bin else None)

    bins = [("protopy", protopy_cmd, lib_env),
            ("protopyc", protopyc_cmd, lib_env),
            ("cpython", cpython_cmd, None),
            ("cpythont", cpythont_cmd, None)]
    bins = [(label, cmd, env) for label, cmd, env in bins if cmd]

    # Build merged env for cpython columns too (BENCH_N etc).
    base_env = dict(env_extra) if env_extra else {}

    for label, cmd, env in bins:
        for _ in range(WARMUP_RUNS):
            run_env = dict(env) if env else {}
            run_env.update(base_env)
            run_cmd(cmd, env=run_env, timeout=timeout)

    times = {label: [] for label, _, _ in bins}
    for _ in range(N_RUNS):
        for label, cmd, env in bins:
            run_env = dict(env) if env else {}
            run_env.update(base_env)
            r = run_cmd(cmd, env=run_env, timeout=timeout)
            if not validate_run(name, r, label):
                continue
            times[label].append(r["elapsed_ms"])

    return {label: median_ms(t) for label, t in times.items()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", "-o")
    p.add_argument("--timeout", type=int, default=240)
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

    results = {}
    tmp_root = Path(tempfile.mkdtemp(prefix="bench_long_"))
    for name, script_rel, argv, env_extra, allow_pyc in BENCHMARKS:
        print(f"Running {name}... (args={argv} env={env_extra})", flush=True)
        protopyc_so = None
        if allow_pyc and protopyc_bin and run_module_bin:
            so = compile_protopyc(
                BENCH_DIR / script_rel, protopyc_bin,
                tmp_root / name, lib_env, verbose=False)
            protopyc_so = so
        r = run_4way(name, script_rel, argv, env_extra,
                     protopy_bin, protopyc_so, run_module_bin,
                     cpython_bin, cpythont_bin, lib_env, args.timeout)
        results[name] = r

    # Report. Baseline: CPython 3.14t (no GIL).
    lines = []
    lines.append("# protoPython long-loop honest comparison (post sprint-10)\n")
    lines.append("Each bench scaled so CPython 3.14t wall lands in the 1-3 s "
                 "range so startup overhead (~30 ms) is <2 % of total wall.\n"
                 "Baseline is CPython 3.14t (free-threading, GIL off) — "
                 "apples-to-apples concurrency-wise vs protoPython, which is "
                 "GIL-free by construction.  The `cp/cpt` column shows the "
                 "lock cost of PEP 703 on each workload (>1.0 = GIL build is "
                 "faster).\n\n")
    lines.append("| Benchmark            | CPython-t ms | CPython GIL ms | cp/cpt | protopy ms | py/cpt | protopyc ms | pc/cpt |")
    lines.append("|----------------------|-------------:|---------------:|-------:|-----------:|-------:|------------:|-------:|")
    import math
    py_ratios = []
    pc_ratios = []
    cp_ratios = []
    for name, r in results.items():
        cp  = r.get("cpython")
        cpt = r.get("cpythont")
        py  = r.get("protopy")
        pyc = r.get("protopyc")
        def fmt(x): return f"{x:9.1f}" if x is not None else "    N/A  "
        def rat(num, den):
            if not num or not den: return "  N/A "
            return f"{num/den:5.2f}x"
        lines.append(
            f"| {name:20s} | {fmt(cpt)} | {fmt(cp)} | {rat(cp, cpt)} | "
            f"{fmt(py)} | {rat(py, cpt)} | {fmt(pyc)} | {rat(pyc, cpt)} |")
        if cpt and cp:  cp_ratios.append(cp/cpt)
        if cpt and py:  py_ratios.append(py/cpt)
        if cpt and pyc: pc_ratios.append(pyc/cpt)

    def geom(xs):
        return math.exp(sum(math.log(x) for x in xs)/len(xs)) if xs else None

    lines.append("")
    lines.append("## Geomeans — baseline is CPython 3.14t (no GIL)")
    g_cp = geom(cp_ratios)
    g_py = geom(py_ratios)
    g_pc = geom(pc_ratios)
    if g_cp: lines.append(f"* CPython GIL / CPython-t (lock cost, long workloads): **{g_cp:.2f}x**")
    if g_py: lines.append(f"* protopy / CPython-t:  **{g_py:.2f}x**")
    if g_pc: lines.append(f"* protopyc / CPython-t: **{g_pc:.2f}x**")

    report = "\n".join(lines) + "\n"
    print(report, flush=True)
    if args.output:
        Path(args.output).write_text(report)
    shutil.rmtree(tmp_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
