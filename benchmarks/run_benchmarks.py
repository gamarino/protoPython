#!/usr/bin/env python3
"""
Benchmark harness for protoPython vs CPython, three-column edition.

For every workload we measure three execution modes:

* **CPython** — the system interpreter (reference).
* **protopy** — protoPython source → AST → bytecode → ExecutionEngine,
  the same path users run interactively.
* **protopyc** — protoPython compiled via the AOT pipeline:
  `protopyc <file>.py --build-so` produces a `module.so` that
  `run_module` loads through `dlopen` + `proto_module_init`.

The protopyc column captures only benchmarks the compiler can lower
correctly; modules that fail to build (e.g. relying on features the
compiler does not yet emit) report N/A for that column without
affecting the CPython / protopy comparison.

Usage:
  PROTOPY_BIN=/path/to/protopy \\
  PROTOPYC_BIN=/path/to/protopyc \\
  RUN_MODULE_BIN=/path/to/run_module \\
  python3 run_benchmarks.py [--output reports/YYYY-MM-DD.md]

Optional env: CPYTHON_BIN=python3.14, LD_LIBRARY_PATH_BENCH=extra:paths.
"""

import argparse
import math
import os
import platform
import shutil
import subprocess
import sys
import tempfile
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
    time_cmd = ["/usr/bin/time", "-f", "%M"] + list(cmd)
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


def compile_protopyc(script, protopyc_bin, work_dir, lib_env, verbose=False):
    """Compile `script` (a .py path) into work_dir/module.so via protopyc.

    Returns the path to module.so on success, None on failure.  Build output
    is captured for the caller's verbose mode; the directory is left
    populated so a follow-up `run_module` call can `dlopen` the .so."""
    work_dir = Path(work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)
    dst = work_dir / Path(script).name
    shutil.copyfile(script, dst)
    so_path = work_dir / "module.so"
    if so_path.exists():
        so_path.unlink()
    try:
        proc = subprocess.run(
            [protopyc_bin, str(dst), "--build-so"],
            cwd=str(work_dir),
            env={**os.environ, **lib_env},
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=120,
        )
    except subprocess.TimeoutExpired:
        if verbose:
            print(f"    protopyc build TIMEOUT for {script.name}", flush=True)
        return None
    if proc.returncode != 0 or not so_path.exists():
        if verbose:
            print(f"    protopyc build FAILED for {script.name}: rc={proc.returncode}", flush=True)
            tail = (proc.stderr or proc.stdout or "")[-400:]
            if tail:
                print(f"      {tail}", flush=True)
        return None
    return so_path


def bench_generic(name, script_name, protopy_bin, cpython_bin,
                  protopyc_bin=None, run_module_bin=None, lib_env=None,
                  timeout=60, trace_file=None, verbose=False):
    """Generic benchmark runner.  Returns a dict with optional times and RSS
    per mode — keys protopy/cpython/protopyc map to (median_ms, median_rss_kb)
    or to None when that mode is unavailable / failed / timed out."""
    script = SCRIPT_DIR / script_name
    if not script.exists():
        return {"protopy": None, "cpython": None, "protopyc": None}
    script_protopy, script_cpy = _script_paths(script)
    lib_env = lib_env or {}

    # Try compiling for protopyc up front; subsequent runs reuse the .so.
    so_path = None
    if protopyc_bin and run_module_bin:
        work_dir = Path(tempfile.mkdtemp(prefix=f"protopyc_bench_{name}_"))
        so_path = compile_protopyc(script, protopyc_bin, work_dir, lib_env, verbose=verbose)

    times_p, times_c, times_pc = [], [], []
    rss_p, rss_c, rss_pc = [], [], []

    for _ in range(WARMUP_RUNS):
        run_cmd([protopy_bin, "--path", PATH_ARG, "--script", script_protopy], timeout=timeout, stderr_file=trace_file, verbose=verbose)
        run_cmd([cpython_bin, script_cpy], timeout=timeout, verbose=verbose)
        if so_path:
            run_cmd([run_module_bin, str(so_path)], cwd=str(so_path.parent),
                    env=lib_env, timeout=timeout, verbose=verbose)

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

        if so_path:
            if verbose: print(f"    {name} protopyc {i+1}/{N_RUNS}:", end="")
            tpc, _, to, rpc = run_cmd(
                [run_module_bin, str(so_path)],
                cwd=str(so_path.parent),
                env=lib_env,
                timeout=timeout, verbose=verbose,
            )
            if not to:
                times_pc.append(tpc)
                rss_pc.append(rpc)

    def pack(times, rss):
        if not times:
            return None
        return (median(times), median(rss) if rss else None)

    return {
        "protopy":  pack(times_p,  rss_p),
        "cpython":  pack(times_c,  rss_c),
        "protopyc": pack(times_pc, rss_pc) if so_path else None,
    }


def _fmt_time(t):
    if t is None:
        return "        N/A"
    return f"{t:>10.2f}"


def _fmt_ratio(num, den):
    if num is None or den is None or den <= 0:
        return "    N/A   "
    r = num / den
    suffix = "x slow" if r >= 1 else "x fast"
    return f"{r:>5.2f}{suffix}"


# Benchmarks excluded from the geomean.  Their ratio against CPython is
# not a meaningful "is the runtime fast?" signal:
#
# * memory_pressure: protoCore deliberately defers garbage collection
#   until the working set forces it (concurrent GC with a tiny
#   stop-the-world window).  The workload's wall time is dominated by
#   how the runtime SCHEDULES collection, not by how fast it executes
#   user code, and the ratio is therefore an indicator of memory
#   policy under stress — not a like-for-like comparison with CPython's
#   reference-counted, eager-deallocation model.  We still report the
#   number for transparency but flag it as INFO and leave it out of the
#   geomean.
GEOMEAN_EXCLUDE = {"memory_pressure"}


def format_report(results):
    """Render the markdown table.  Each result is a dict
    {protopy: (t, rss), cpython: (...), protopyc: (...)} where each entry
    may be None for an unavailable mode."""
    header = "| Benchmark              | CPython (ms) | protopy (ms) | protopyc (ms) | py/cp        | pc/cp        | RSS py/pc/cp        |"
    sep    = "|------------------------|--------------|--------------|---------------|--------------|--------------|---------------------|"
    lines = [
        f"# protoPython performance audit — {datetime.now(timezone.utc).strftime('%Y-%m-%d')}",
        "",
        f"Platform: {platform.system()} {platform.machine()}, median of {N_RUNS} runs (timeouts excluded).",
        "",
        "Three execution modes per workload:",
        "* **CPython** — the system interpreter (reference).",
        "* **protopy** — protoPython bytecode interpreter.",
        "* **protopyc** — protoPython AOT-compiled to C++ via `protopyc --build-so`, loaded as a shared object.",
        "",
        "Ratios are protoPython-mode / CPython-time: <1.0 = faster than CPython, >1.0 = slower.",
        "Rows tagged `[INFO]` are reported for transparency but do NOT participate in the geomean — see the report footer.",
        "",
        header,
        sep,
    ]

    py_ratios = []
    pc_ratios = []
    info_notes = []

    for name, modes in results.items():
        cp = modes.get("cpython")
        py = modes.get("protopy")
        pc = modes.get("protopyc")

        tp = py[0] if py else None
        tc = cp[0] if cp else None
        tpc = pc[0] if pc else None
        rp = py[1] if py and py[1] else None
        rc = cp[1] if cp and cp[1] else None
        rpc = pc[1] if pc and pc[1] else None

        py_str  = _fmt_time(tp)
        cp_str  = _fmt_time(tc)
        pc_str  = _fmt_time(tpc)
        py_ratio_str = _fmt_ratio(tp,  tc)
        pc_ratio_str = _fmt_ratio(tpc, tc) if tpc is not None else "    N/A   "

        in_geomean = name not in GEOMEAN_EXCLUDE
        if in_geomean:
            if tp is not None and tc and tc > 0:
                py_ratios.append(tp / tc)
            if tpc is not None and tc and tc > 0:
                pc_ratios.append(tpc / tc)
        else:
            info_notes.append(name)

        rss_parts = []
        for r in (rp, rpc, rc):
            rss_parts.append(f"{r/1024:>5.1f}" if r else "  N/A")
        rss_str = "/".join(rss_parts) + " MB"

        label = name + (" [INFO]" if not in_geomean else "")
        lines.append(
            f"| {label:<22} | {cp_str}   | {py_str}   | {pc_str}    | {py_ratio_str:<12} | {pc_ratio_str:<12} | {rss_str:<19} |"
        )

    def geomean(xs):
        xs = [x for x in xs if x and x > 0]
        if not xs:
            return None
        return math.exp(sum(math.log(x) for x in xs) / len(xs))

    py_gm = geomean(py_ratios)
    pc_gm = geomean(pc_ratios)
    py_gm_str = f"{py_gm:>5.2f}x" if py_gm else "  N/A"
    pc_gm_str = f"{pc_gm:>5.2f}x" if pc_gm else "  N/A"
    lines.append(sep)
    n_rows = len(py_ratios)
    lines.append(
        f"| {'Geomean (n='+str(n_rows)+')':<22} | {'':>10}   | {'':>10}   | {'':>10}    | {py_gm_str:<12} | {pc_gm_str:<12} | {'':<19} |"
    )

    if info_notes:
        lines.append("")
        lines.append("`[INFO]` rows excluded from the geomean:")
        for nm in info_notes:
            if nm == "memory_pressure":
                lines.append(
                    "* **memory_pressure** — protoCore defers GC until the working "
                    "set forces it (concurrent collector, tiny STW window).  The wall "
                    "time on this workload reflects collection scheduling under "
                    "stress, not user-code throughput; the ratio against CPython's "
                    "reference-counted eager-deallocation model is not "
                    "comparable apples-to-apples.  Reported for transparency."
                )
            else:
                lines.append(f"* **{nm}** — excluded.")

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", "-o")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--quick", "-q", action="store_true")
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    global N_RUNS, WARMUP_RUNS
    if args.quick: N_RUNS, WARMUP_RUNS = 2, 1
    
    protopy_bin = os.environ.get("PROTOPY_BIN")
    if not protopy_bin:
        print("Set PROTOPY_BIN environment variable.")
        return 1
    cpython_bin = os.environ.get("CPYTHON_BIN", "python3")
    protopyc_bin = os.environ.get("PROTOPYC_BIN")
    run_module_bin = os.environ.get("RUN_MODULE_BIN")
    extra_ld = os.environ.get("LD_LIBRARY_PATH_BENCH", "")
    lib_env = {"LD_LIBRARY_PATH": extra_ld + (":" + os.environ.get("LD_LIBRARY_PATH", "") if os.environ.get("LD_LIBRARY_PATH") else "")} if extra_ld else {}

    # Auto-derive paths from PROTOPY_BIN layout when the user did not set
    # PROTOPYC_BIN / RUN_MODULE_BIN explicitly.  PROTOPY_BIN typically lives
    # at <build>/src/runtime/protopy; protopyc is at <build>/src/compiler/
    # protopyc and the run_module helper sits in <repo>/test/compiler/.  We
    # also seed LD_LIBRARY_PATH so the loader can find libprotoPython /
    # libprotoCore that the compiled .so links against.
    if protopy_bin:
        bin_path = Path(protopy_bin).resolve()
        build_dir = bin_path.parent.parent.parent if len(bin_path.parts) > 3 else None
        if build_dir:
            if not protopyc_bin:
                guess = build_dir / "src" / "compiler" / "protopyc"
                if guess.is_file():
                    protopyc_bin = str(guess)
            if not run_module_bin:
                rm_guess = PROJECT_ROOT / "test" / "compiler" / "run_module"
                if rm_guess.is_file():
                    run_module_bin = str(rm_guess)
            if not extra_ld:
                ld_parts = [
                    str(build_dir / "src" / "library"),
                    str(build_dir / "protoCore"),
                ]
                existing = os.environ.get("LD_LIBRARY_PATH", "")
                lib_env = {"LD_LIBRARY_PATH": ":".join(ld_parts + ([existing] if existing else []))}

    if protopyc_bin and run_module_bin:
        print(f"protopyc column enabled: protopyc={protopyc_bin}, run_module={run_module_bin}")
    else:
        print("protopyc column DISABLED (set PROTOPYC_BIN + RUN_MODULE_BIN to enable).")

    # Stale-binary guard: protopy links to libprotoCore.so from a sibling
    # protoCore build directory.  If a release-mode build_release/ exists
    # next to the build/ the user pointed at, warn — the protoCore code
    # probably moved underneath and the user is measuring an outdated
    # snapshot.  Caught us out in May 2026 when the protoJS bench runner
    # silently used build/ for an entire optimisation cycle while
    # build_release/ tracked the actual fixes.  Honour PROTOPY_NOWARN to
    # silence in CI configs that pin specific build dirs intentionally.
    if not os.environ.get("PROTOPY_NOWARN"):
        bin_dir = os.path.dirname(os.path.dirname(os.path.dirname(protopy_bin)))
        bin_basename = os.path.basename(bin_dir)
        if bin_basename == "build":
            sibling_release = os.path.join(os.path.dirname(bin_dir), "build_release",
                                            "src", "runtime", "protopy")
            if os.path.isfile(sibling_release):
                bin_mtime = os.path.getmtime(protopy_bin)
                rel_mtime = os.path.getmtime(sibling_release)
                if rel_mtime > bin_mtime + 60:   # >1 min newer
                    age_h = (rel_mtime - bin_mtime) / 3600
                    print(f"⚠ Warning: PROTOPY_BIN points at build/ which is "
                          f"{age_h:.1f} h older than the available build_release/. "
                          f"You are likely measuring stale code.  Set "
                          f"PROTOPY_BIN={sibling_release} or "
                          f"PROTOPY_NOWARN=1 to silence.", flush=True)
    
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
            # No source file; the protopyc column is not meaningful here.
            times_p, times_c, rss_p, rss_c = [], [], [], []
            for _ in range(WARMUP_RUNS):
                run_cmd([protopy_bin, "--module", "abc"])
                run_cmd([cpython_bin, "-c", "import abc"])
            for _ in range(N_RUNS):
                tp, _, _, rp = run_cmd([protopy_bin, "--module", "abc"])
                times_p.append(tp); rss_p.append(rp)
                tc, _, _, rc = run_cmd([cpython_bin, "-c", "import abc"])
                times_c.append(tc); rss_c.append(rc)
            results[name] = {
                "protopy":  (median(times_p), median(rss_p)) if times_p else None,
                "cpython":  (median(times_c), median(rss_c)) if times_c else None,
                "protopyc": None,
            }
        else:
            results[name] = bench_generic(
                name, script, protopy_bin, cpython_bin,
                protopyc_bin=protopyc_bin, run_module_bin=run_module_bin,
                lib_env=lib_env,
                timeout=args.timeout, verbose=args.verbose,
            )

    report = format_report(results)
    print(report)
    if args.output:
        Path(args.output).write_text(report)
    return 0

if __name__ == "__main__":
    sys.exit(main())


if __name__ == "__main__":
    sys.exit(main())
