#!/usr/bin/env python3
"""Run the CPython test-file subset under protopy and report unittest counts.

Usage:
    python3 tests/run_cpython_subset.py [--root DIR] [--protopy PATH]
                                        [--json OUT.json] [--compare BASE.json]
                                        [--only NAME ...] [--timeout-scale X]
                                        [--log-dir DIR]

Each file in test/cpython/ (plus tests/test_os.py and tests/test_decorator.py)
runs in its own protopy process with stdin at EOF.  For every file the report
gives the exit code, the wall time, and what unittest printed: "Ran N tests"
and the "OK" / "FAILED (failures=F, errors=E, skipped=S)" summary.  A file
that printed no summary is reported as such (NO-SUMMARY), never as a pass.

PER-FILE TIME LIMITS.  A single limit for every file does not work: most files
finish in a few seconds, while test_datetime.py runs 3,598 tests (pure and C
variants) and took 115-122 s in earlier comparison runs (82.8 s alone on an
idle machine), so a flat 120 s limit decided its verdict by machine load.  Each file's limit is
therefore derived from its measured time:

    limit = max(DEFAULT_LIMIT, ceil(LIMIT_FACTOR * measured))

with the measurements in MEASURED_SECONDS below.  A file that runs longer than
WARN_FRACTION of its limit is flagged SLOW in the report, so a growing runtime
is noticed and re-measured before it turns into a TIMEOUT.  A file that hits
its limit is reported as TIMEOUT with the counts it printed so far; it is
never retried.  --timeout-scale multiplies every limit (for a slower machine
or a sanitizer build) and is printed in the report.

--log-dir keeps each file's full output as DIR/<path>.out, the path with
"/" replaced by "_" (test/cpython/test_os.py and tests/test_os.py differ).

--compare reads a JSON report written by --json and prints, per file, the
base and current counts side by side.
"""

import argparse
import glob
import json
import math
import os
import re
import subprocess
import sys
import time

# Wall-clock seconds on DEV12 (AMD Lucienne, 6 cores / 12 threads, Release
# build, protoCore 43fae387), the slowest observed run: test_datetime took
# 82.8 s alone at main 9e33799a and up to 122 s in loaded comparison runs.
# Re-measure when a file is flagged SLOW.
MEASURED_SECONDS = {
    "test_datetime.py": 122,
}
DEFAULT_LIMIT = 120
LIMIT_FACTOR = 2.5
WARN_FRACTION = 0.6

EXTRA_FILES = ["tests/test_os.py", "tests/test_decorator.py"]

RAN_RE = re.compile(r"^Ran (\d+) tests? in ([0-9.]+)s", re.M)
FAILED_RE = re.compile(r"^FAILED \(([^)]*)\)", re.M)
OK_RE = re.compile(r"^OK(?: \(([^)]*)\))?\s*$", re.M)


def limit_for(name, scale):
    measured = MEASURED_SECONDS.get(name)
    base = DEFAULT_LIMIT if measured is None else max(DEFAULT_LIMIT, math.ceil(LIMIT_FACTOR * measured))
    return int(math.ceil(base * scale))


def parse_counts(text):
    """Counts from the LAST unittest summary in the output (a file may run
    several suites; the last summary is the one the process ended on)."""
    ran = RAN_RE.findall(text)
    out = {"ran": None, "failures": 0, "errors": 0, "skipped": 0,
           "expected_failures": 0, "unexpected_successes": 0, "summary": None}
    if not ran:
        return out
    out["ran"] = int(ran[-1][0])
    tail = text[text.rfind("Ran " + ran[-1][0]):]
    m = FAILED_RE.search(tail)
    fields = None
    if m:
        out["summary"] = "FAILED"
        fields = m.group(1)
    else:
        m = OK_RE.search(tail)
        if m:
            out["summary"] = "OK"
            fields = m.group(1) or ""
    if fields:
        for part in fields.split(","):
            if "=" not in part:
                continue
            k, v = part.strip().split("=", 1)
            if k in out and v.strip().isdigit():
                out[k] = int(v)
    return out


def run_file(protopy, root, rel, limit, log_dir=None):
    env = os.environ.copy()
    env["PROTO_PYTHONPATH"] = os.path.join(root, "lib", "python3.14")
    start = time.monotonic()
    timed_out = False
    try:
        proc = subprocess.run([protopy, os.path.join(root, rel)], cwd=root, env=env,
                              stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=limit)
        rc = proc.returncode
        raw = proc.stdout
    except subprocess.TimeoutExpired as e:
        timed_out = True
        rc = None
        raw = e.stdout or b""
    elapsed = time.monotonic() - start
    text = raw.decode("utf-8", "replace")
    if log_dir:
        with open(os.path.join(log_dir, rel[:-3].replace("/", "_") + ".out"), "w") as fh:
            fh.write(text)
    counts = parse_counts(text)
    if timed_out:
        status = "TIMEOUT"
    elif counts["summary"] is None:
        status = "NO-SUMMARY" if rc != 0 else "EXIT-0"
    else:
        status = counts["summary"]
    last = [ln for ln in text.splitlines() if ln.strip()][-1:] or [""]
    return {"file": rel, "rc": rc, "status": status, "seconds": round(elapsed, 1),
            "limit": limit, "slow": (not timed_out) and elapsed > WARN_FRACTION * limit,
            "last_line": last[0][:160], **counts}


def fmt_counts(r):
    if r is None:
        return "-"
    if r["ran"] is None:
        return "%s rc=%s" % (r["status"], r["rc"])
    return "%s ran=%d F=%d E=%d S=%d" % (r["status"], r["ran"], r["failures"], r["errors"], r["skipped"])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--protopy", default=None)
    ap.add_argument("--json", dest="json_out")
    ap.add_argument("--compare")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--timeout-scale", type=float, default=1.0)
    ap.add_argument("--log-dir")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    protopy = args.protopy or os.path.join(root, "build_release", "src", "runtime", "protopy")
    if sys.platform == "win32" and not protopy.endswith(".exe"):
        protopy += ".exe"
    if not os.path.isfile(protopy):
        sys.exit("protopy not found: %s" % protopy)

    files = sorted(os.path.relpath(p, root) for p in glob.glob(os.path.join(root, "test", "cpython", "*.py")))
    files += [f for f in EXTRA_FILES if os.path.isfile(os.path.join(root, f))]
    if args.only:
        files = [f for f in files if os.path.basename(f) in args.only or f in args.only]

    base = {}
    if args.compare:
        with open(args.compare) as fh:
            base = {r["file"]: r for r in json.load(fh)["results"]}

    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)
    print("protopy: %s\ntimeout scale: %g\n" % (protopy, args.timeout_scale))
    results = []
    for rel in files:
        r = run_file(protopy, root, rel, limit_for(os.path.basename(rel), args.timeout_scale), args.log_dir)
        results.append(r)
        flag = " SLOW (%.0f%% of the %d s limit: re-measure)" % (100 * r["seconds"] / r["limit"], r["limit"]) if r["slow"] else ""
        line = "%-36s %-40s %6.1fs%s" % (rel, fmt_counts(r), r["seconds"], flag)
        if args.compare:
            line = "%-36s base: %-40s now: %-40s" % (rel, fmt_counts(base.get(rel)), fmt_counts(r))
        print(line, flush=True)

    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump({"protopy": protopy, "timeout_scale": args.timeout_scale, "results": results}, fh, indent=1)

    timeouts = [r["file"] for r in results if r["status"] == "TIMEOUT"]
    if timeouts:
        print("\nTIMEOUT (not a pass, not retried): " + ", ".join(timeouts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
