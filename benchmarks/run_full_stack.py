#!/usr/bin/env python3
"""Full-stack benchmark: protoCpp (cpp / proto / proto_fast) plus
protoPython (protopy / protopyc) plus CPython 3.14 (GIL on / off).

Per bench:
  * For protoCpp binaries: a single child process; wall-clock = inner
    work (startup is ~1 ms — negligible at our N).
  * For Python binaries: the bench prints `BENCH_RESULT ... ms=<inner>`
    on its last line.  We parse that as the inner time, ignoring the
    interpreter's startup + GC tail.

The same workload size is used by every variant of a given bench so the
numbers are directly comparable.  Baseline for ratios is CPython 3.14t
(free-threading, GIL off) — apples-to-apples concurrency-wise vs
protoPython, and the canonical "Python without the GIL" reference.

Runs every bench interleaved across all binaries (warmup ×1, then
N=3 interleaved measurement rounds) so transient system load hits
each column equally.
"""
import argparse
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from run_benchmarks import (  # noqa: E402
    SCRIPT_DIR, PROJECT_ROOT, compile_protopyc,
)
BENCH_DIR = SCRIPT_DIR
PROTOCPP_BUILD = Path("/home/gamarino/Documentos/proyectos/protoCpp/build_release")

N_RUNS = 3
WARMUP_RUNS = 1

BENCH_RESULT_RE = re.compile(r"BENCH_RESULT\b.*\bms=([0-9]+(?:\.[0-9]+)?)")

# Per-bench:
#   - name (also the protoCpp suffix)
#   - python script (relative to BENCH_DIR)
#   - argv passed to python script (if needed)
#   - env BENCH_N value (or None to omit)
# Sizes match protoCpp (so the C++/protoCore/Python numbers are
# directly comparable).
BENCHES = [
    ("int_sum_loop",     "int_sum_loop.py",     [], "10000000"),
    ("attr_lookup",      "attr_lookup.py",      [], "5000000"),
    ("list_append_loop", "list_append_loop.py", [], "10000"),
    ("str_concat_loop",  "str_concat_loop.py",  [], "2000"),
    ("call_recursion",   "call_recursion.py",   [], "25"),
    ("multithread_cpu",  "multithreaded_cpu.py",[], None),
]


def time_protocpp(binary, timeout):
    """Run a protoCpp binary and return wall-clock ms.

    protoCpp binaries print the result on stdout; we accept any
    non-empty stdout as a sane success indicator and the wall as the
    measurement.  Exit code != 0 -> None.
    """
    if not binary.exists():
        return None
    t0 = time.perf_counter()
    try:
        r = subprocess.run([str(binary)], capture_output=True, text=True,
                           timeout=timeout, cwd=str(binary.parent))
    except subprocess.TimeoutExpired:
        return None
    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    if r.returncode != 0:
        return None
    if not (r.stdout or "").strip():
        return None
    return elapsed_ms


def time_python(cmd, env, timeout):
    """Run a python-side bench and return inner ms (parsed from
    BENCH_RESULT).  Returns None if exit !=0 or no BENCH_RESULT line.
    """
    full_env = {**os.environ, **(env or {})}
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, env=full_env)
    except subprocess.TimeoutExpired:
        return None
    if r.returncode != 0:
        return None
    out = r.stdout or ""
    for line in reversed(out.splitlines()):
        m = BENCH_RESULT_RE.search(line)
        if m:
            return float(m.group(1))
    return None


def median(xs):
    xs = [x for x in xs if x is not None and x > 0]
    return statistics.median(xs) if xs else None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", "-o")
    p.add_argument("--timeout", type=int, default=180)
    args = p.parse_args()

    protopy_bin    = os.environ["PROTOPY_BIN"]
    protopyc_bin   = os.environ.get("PROTOPYC_BIN")
    run_module_bin = os.environ.get("RUN_MODULE_BIN")
    cpython_bin    = os.environ["CPYTHON_BIN"]
    cpythont_bin   = os.environ["CPYTHONT_BIN"]
    extra_ld = os.environ.get("LD_LIBRARY_PATH_BENCH", "")
    if not extra_ld:
        build_dir = Path(protopy_bin).resolve().parent.parent.parent
        extra_ld = ":".join([str(build_dir / "src" / "library"),
                             str(build_dir / "protoCore")])
    lib_env = {"LD_LIBRARY_PATH":
               extra_ld + (":" + os.environ.get("LD_LIBRARY_PATH", ""))}

    results = {}
    tmp_root = Path(tempfile.mkdtemp(prefix="bench_full_"))

    for name, script_rel, argv, bench_n in BENCHES:
        print(f"=== {name} ===", flush=True)
        # Build per-binary commands / source.
        abs_script = (BENCH_DIR / script_rel).resolve()
        rel_script = str(abs_script.relative_to(PROJECT_ROOT))
        protopy_cmd  = [protopy_bin, rel_script] + argv
        cpython_cmd  = [cpython_bin, str(abs_script)] + argv
        cpythont_cmd = [cpythont_bin, str(abs_script)] + argv
        py_env = {"BENCH_N": bench_n} if bench_n else {}

        # protopyc: compile once, reuse.
        protopyc_cmd = None
        if protopyc_bin and run_module_bin:
            so = compile_protopyc(abs_script, protopyc_bin,
                                  tmp_root / name, lib_env, verbose=False)
            if so:
                protopyc_cmd = [run_module_bin, str(so)] + argv

        # protoCpp binaries.
        cpp_bin       = PROTOCPP_BUILD / f"bench_cpp_{name}"
        proto_bin     = PROTOCPP_BUILD / f"bench_proto_{name}"
        protofast_bin = PROTOCPP_BUILD / f"bench_proto_fast_{name}"

        # Per-mode samples.
        samples = {"cpp": [], "proto": [], "proto_fast": [],
                   "protopy": [], "protopyc": [],
                   "cpython": [], "cpythont": []}

        def round_once():
            samples["cpp"].append(time_protocpp(cpp_bin, args.timeout))
            samples["proto"].append(time_protocpp(proto_bin, args.timeout))
            samples["proto_fast"].append(time_protocpp(protofast_bin, args.timeout))
            samples["protopy"].append(time_python(
                protopy_cmd, {**lib_env, **py_env}, args.timeout))
            if protopyc_cmd:
                samples["protopyc"].append(time_python(
                    protopyc_cmd, {**lib_env, **py_env}, args.timeout))
            samples["cpython"].append(time_python(
                cpython_cmd, py_env, args.timeout))
            samples["cpythont"].append(time_python(
                cpythont_cmd, py_env, args.timeout))

        # Warmup.
        for _ in range(WARMUP_RUNS):
            round_once()
        samples = {k: [] for k in samples}  # discard warmup samples
        # Measurement.
        for _ in range(N_RUNS):
            round_once()

        results[name] = {k: median(v) for k, v in samples.items()}
        for k, v in results[name].items():
            print(f"  {k:11s}: {v}", flush=True)

    # Report — baseline is cpythont.
    def fmt_ms(x):
        return f"{x:8.2f}" if x is not None else "    N/A "
    def fmt_ratio(num, den):
        if not num or not den:
            return " N/A "
        return f"{num/den:5.2f}x"

    lines = []
    lines.append("# Full-stack honest comparison (cpp / protoCore / protoPython / CPython)")
    lines.append("")
    lines.append("Inner-only timing for Python variants (parsed from each bench's")
    lines.append("`BENCH_RESULT ms=` marker — excludes startup + GC tail).  Wall-clock")
    lines.append("for protoCpp binaries (startup is ~1 ms there).  Same workload size")
    lines.append("(matching the protoCpp benches) across all seven columns so the")
    lines.append("numbers are directly comparable.\n")
    lines.append("**Baseline 1.0 = CPython 3.14t (free-threading, GIL off).**\n")
    lines.append("Columns:")
    lines.append("* `cpp`         — pure C++, hardware floor (no protoCore).")
    lines.append("* `proto`       — protoCore directly from C++ (kernel floor).")
    lines.append("* `proto_fast`  — protoCore with API-side optimisations.")
    lines.append("* `cp` / `cpt`  — CPython 3.14 with GIL on / off (uv builds).")
    lines.append("* `protopy`     — protoPython bytecode interpreter.")
    lines.append("* `protopyc`    — protoPython AOT to C++.\n")

    cols = ["cpp", "proto", "proto_fast", "cp", "cpt", "protopy", "protopyc"]
    label_for = {"cpp": "cpp", "proto": "proto", "proto_fast": "proto_fast",
                 "cp": "cpython", "cpt": "cpythont", "protopy": "protopy",
                 "protopyc": "protopyc"}

    # ms table
    lines.append("## Absolute wall-time / inner-time (ms)")
    header = "| Bench                | " + " | ".join(f"{c:>10}" for c in cols) + " |"
    sep = "|" + "----------------------|" + "|".join(["-" * 12 for _ in cols]) + "|"
    lines.append(header)
    lines.append(sep)
    for name, _, _, _ in BENCHES:
        r = results[name]
        row = "| " + f"{name:20s}" + " | " + " | ".join(
            fmt_ms(r.get(label_for[c])) for c in cols) + " |"
        lines.append(row)

    # ratio table (vs cpt)
    lines.append("")
    lines.append("## Ratios vs CPython 3.14t (no GIL)")
    header = "| Bench                | " + " | ".join(f"{c:>8}" for c in cols) + " |"
    sep = "|" + "----------------------|" + "|".join(["-" * 10 for _ in cols]) + "|"
    lines.append(header)
    lines.append(sep)
    geom = {c: [] for c in cols}
    for name, _, _, _ in BENCHES:
        r = results[name]
        baseline = r.get("cpythont")
        if not baseline:
            continue
        row = "| " + f"{name:20s}" + " | " + " | ".join(
            fmt_ratio(r.get(label_for[c]), baseline) for c in cols) + " |"
        lines.append(row)
        for c in cols:
            v = r.get(label_for[c])
            if v and baseline:
                geom[c].append(v / baseline)

    def gm(xs):
        if not xs: return None
        return math.exp(sum(math.log(x) for x in xs) / len(xs))
    lines.append("")
    lines.append("## Geomeans (baseline = cpythont = 1.0)")
    for c in cols:
        g = gm(geom[c])
        if g is not None:
            lines.append(f"* `{c}` ({label_for[c]}): **{g:.2f}x**")

    report = "\n".join(lines) + "\n"
    print(report, flush=True)
    if args.output:
        Path(args.output).write_text(report)
    shutil.rmtree(tmp_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
