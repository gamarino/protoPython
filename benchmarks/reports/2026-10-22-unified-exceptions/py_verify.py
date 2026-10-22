#!/usr/bin/env python3
"""Self-report check for protoPython: every harness benchmark and every probe
runs once with protopy and protopyc (BEFORE and AFTER) and CPython 3.14t; the
reported work (stdout with timings stripped) must be identical."""
import os, re, subprocess, sys, shutil
M = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]
CPT = os.path.expanduser("~/.local/bin/python3.14t")
B = os.path.join(M, "after/protoPython/benchmarks")
benches = ["int_sum_loop.py", "list_append_loop.py", "str_concat_loop.py", "range_iterate.py",
           "multithreaded_cpu.py", "attr_lookup.py", "call_recursion.py", "memory_pressure.py",
           "pyperf/bench_fib.py", "pyperf/bench_binary_trees.py", "pyperf/bench_nqueens.py",
           "pyperf/bench_richards_lite.py", "pyperf/bench_sieve.py"]
probes = sorted(f for f in os.listdir(os.path.join(M, "py_probes")) if f.endswith(".py"))
TIMING = re.compile(r"\b(ms|min|max|median|mean|avg|time|elapsed|t|best|worst|s)=\s*[-0-9.e]+\w*", re.I)
def norm(out):
    CHATTER = ("Initializing Python environment", "Initializing __main__ module", "getGlobals() returned",
               "Loading shared library", "Finding proto_module_init", "Calling proto_module_init", "Cleaning up", "Done.")
    lines = [TIMING.sub(r"\1=_", l) for l in out.splitlines() if l.strip() and not l.startswith(CHATTER)]
    lines = [re.sub(r"\d+\.\d+\s*(ms|s)\b", "_", l) for l in lines]
    return lines[-3:]
def libenv(v):
    t = os.path.join(M, v, "protoPython/build_release")
    return {**os.environ, "LD_LIBRARY_PATH": os.path.join(t, "src/library") + ":" + os.path.join(OUT, "libs", "verify_" + v)}
def run(cmd, env=None, cwd=None):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=600, env=env, cwd=cwd)
    return p.returncode, p.stdout, p.stderr[-300:]
bad = 0
for kind, d, files in (("bench", B, benches), ("probe", os.path.join(M, "py_probes"), probes)):
    for f in files:
        src = os.path.join(d, f)
        res = {"cpythont": run([CPT, src])}
        for v in ("before", "after"):
            t = os.path.join(M, v, "protoPython/build_release")
            env = libenv(v)
            res["protopy_" + v] = run([t + "/src/runtime/protopy", src], env, cwd=os.path.join(M, v, "protoPython"))
            wd = os.path.join(OUT, "verify_pyc", v, os.path.basename(f)[:-3]); os.makedirs(wd, exist_ok=True)
            shutil.copyfile(src, os.path.join(wd, os.path.basename(f)))
            c = run([t + "/src/compiler/protopyc", os.path.basename(f), "--build-so"], env, cwd=wd)
            if c[0] == 0 and os.path.exists(os.path.join(wd, "module.so")):
                res["protopyc_" + v] = run([t + "/test/compiler/run_module", os.path.join(wd, "module.so")], env, cwd=wd)
            else:
                res["protopyc_" + v] = (c[0], "COMPILE_FAILED", c[2])
        ref = norm(res["cpythont"][1])
        for k, (rc, o, e) in res.items():
            ok = rc == 0 and norm(o) == ref
            if not ok: bad += 1
            print("%-4s %-5s %-34s %-16s rc=%s %s%s" % ("OK" if ok else "BAD", kind, f, k, rc, norm(o)[-1:] if o else o,
                  "" if ok else "  ref=%s err=%r" % (ref[-1:], e)), flush=True)
print("VERIFY_BAD", bad)
