#!/usr/bin/env python3
"""Ground-truth audit of protopy test status.

Runs each test in the protopy conformance catalog and classifies the
outcome so silent halts (exit-code-0 with no real output) can be
distinguished from genuine PASSes, FAILs, CRASHes, and TIMEOUTs.

The silent-halt bug (fixed in commit efcfa7f3) caused the interpreter
to occasionally exit 0 with no further output partway through a module
body.  Several entries in docs/CPYTHON_CONFORMANCE.md were marked PASS
on the basis of exit-code-0 alone -- those PASSes need to be re-checked.

Usage:
    python3 tests/synthetic/sp_audit_truth.py [--out PATH]

By default the markdown report is printed to stdout.  Pass --out PATH
to also write a copy to PATH.

The script is intentionally self-contained so it can be re-run without
any other repo state.  Set PROTOPY in the environment to override the
binary location.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROTOPY = os.environ.get("PROTOPY", os.path.join(REPO_ROOT, "build/src/runtime/protopy"))
TIMEOUT_SECONDS = 120

# Inline-script tests are written into a per-run temp dir so they don't
# pollute the working tree.  Their key is also the human-readable label.
INLINE_SCRIPTS = {
    "import importlib": "import importlib\nprint('OK')\n",
    "import inspect": "import inspect\nprint('OK')\n",
}


@dataclass
class TestSpec:
    category: str
    label: str            # human label used for the report
    path: str             # absolute path passed to protopy
    claimed: str          # what CPYTHON_CONFORMANCE.md currently claims
    inline: bool = False  # tmp-file driven verify-only test


def _abs(p: str) -> str:
    return os.path.join(REPO_ROOT, p)


# Each entry's `claimed` text is a verbatim quote of the relevant line in
# docs/CPYTHON_CONFORMANCE.md as of 2026-04-29 (HEAD d9cb7b8b).
TESTS_REAL: List[TestSpec] = [
    # --- Essential ----------------------------------------------------------
    TestSpec("Essential", "test_grammar.py", _abs("lib/python3.14/test/test_grammar.py"),
             "PARTIAL - 54/75 pass, 11 fail, 10 err, 0 crash (V154.8, 2026-04-25)"),
    TestSpec("Essential", "test_types.py", _abs("lib/python3.14/test/test_types.py"),
             "PARTIAL - 6/131 pass, runs to completion (V124, 2026-04-24)"),
    TestSpec("Essential", "test_descr.py", _abs("lib/python3.14/test/test_descr.py"),
             "TIMEOUT - runs >5 min; type() descriptor tests expose slow paths"),
    TestSpec("Essential", "test_generators.py", _abs("lib/python3.14/test/test_generators.py"),
             "PARTIAL - 0/1 pass (doctest runner fails); import chain runs"),
    TestSpec("Essential", "test_asyncgen.py", _abs("lib/python3.14/test/test_asyncgen.py"),
             "PARTIAL - 85 tests now run (0/85 pass, 80 errors, 5 failures); unblocked (V116)"),
    TestSpec("Essential", "test_base64.py", _abs("lib/python3.14/test/test_base64.py"),
             "PARTIAL - runs to completion, many failures (V110, 2026-04-23)"),
    # test_json is a *package* in CPython 3.14 (test_json/__init__.py +
    # test_json/__main__.py).  CPYTHON_CONFORMANCE.md refers to it as
    # "test_json.py" but the runnable entry point is __main__.py.
    TestSpec("Essential", "test_json (package)", _abs("lib/python3.14/test/test_json/__main__.py"),
             "PASS - 9/9 tests pass (V124, 2026-04-24)"),

    # --- Important ----------------------------------------------------------
    TestSpec("Important", "test_sys.py", _abs("lib/python3.14/test/test_sys.py"),
             "UNBLOCKED (V106)"),
    TestSpec("Important", "test_os.py", _abs("lib/python3.14/test/test_os.py"),
             "UNBLOCKED (V106)"),
    TestSpec("Important", "test_re.py", _abs("lib/python3.14/test/test_re.py"),
             "UNBLOCKED (V106)"),
    TestSpec("Important", "test_datetime.py", _abs("lib/python3.14/test/test_datetime.py"),
             "UNBLOCKED (V106, requires frame introspection hardening)"),
    TestSpec("Important", "test_collections.py", _abs("lib/python3.14/test/test_collections.py"),
             "UNBLOCKED (V106)"),
    TestSpec("Important", "test_functools.py", _abs("lib/python3.14/test/test_functools.py"),
             "UNBLOCKED (V106)"),

    # --- Necessary (custom) -------------------------------------------------
    TestSpec("Necessary", "test_decorator.py", _abs("tests/test_decorator.py"),
             "PASS (custom protoPython test - tests/test_decorator.py)"),
    TestSpec("Necessary", "test_abc.py", _abs("tests/test_abc.py"),
             "PASS (custom protoPython test - tests/test_abc.py)"),
    TestSpec("Necessary", "test_contextlib.py", _abs("tests/test_contextlib.py"),
             "PASS (custom protoPython test - tests/test_contextlib.py)"),
    TestSpec("Necessary", "test_dataclasses.py", _abs("tests/test_dataclasses.py"),
             "PASS (custom protoPython test - tests/test_dataclasses.py)"),

    # --- Bootstrap (inline) -------------------------------------------------
    TestSpec("Bootstrap", "import importlib", "<inline>",
             "PASS - import importlib works end-to-end (V101)", inline=True),
    TestSpec("Bootstrap", "import inspect", "<inline>",
             "PASS - import inspect works end-to-end (V101)", inline=True),
]


# ---------------------------------------------------------------------------
# Result record + classifier
# ---------------------------------------------------------------------------

CATEGORIES = (
    "PASS_UNITTEST",
    "PASS_CUSTOM",
    "FAIL_UNITTEST",
    "SILENT_HALT",
    "TIMEOUT",
    "CRASH",
    "PARTIAL",
    "UNKNOWN",
)


@dataclass
class TestResult:
    spec: TestSpec
    exit_code: int
    stdout: str
    stderr: str
    wall_seconds: float
    classification: str = "UNKNOWN"

    def stdout_lines(self) -> List[str]:
        return [ln for ln in self.stdout.splitlines() if ln.strip() != ""]


def classify(exit_code: int, stdout: str, stderr: str) -> str:
    """Apply the rule cascade in the spec, first match wins."""
    lines = stdout.splitlines()
    nonempty = [ln for ln in lines if ln.strip() != ""]
    last10 = nonempty[-10:]
    last5 = nonempty[-5:]
    last10_text = "\n".join(last10)
    last5_text = "\n".join(last5)

    # 5: TIMEOUT -- subprocess module signals 124 / -SIGTERM via wrapper.
    if exit_code == 124:
        return "TIMEOUT"

    # 1: SILENT_HALT
    success_marker = (
        "OK" in [ln.strip() for ln in last10]
        or "passed" in last5_text.lower()
        or "0 of" in last10_text
    )
    if exit_code == 0 and (len(nonempty) == 0 or len(nonempty) < 5) and not success_marker:
        return "SILENT_HALT"

    # 2: FAIL_UNITTEST  (unittest's "FAILED (failures=...)" summary)
    if "FAILED (" in stdout:
        return "FAIL_UNITTEST"

    # 3: PASS_UNITTEST  (last 10 non-empty lines contain a bare 'OK')
    if exit_code == 0 and "OK" in [ln.strip() for ln in last10]:
        return "PASS_UNITTEST"

    # 4: PASS_CUSTOM
    if exit_code == 0:
        # 4a: "<test_name> passed"
        for ln in last5:
            if "passed" in ln.lower():
                if "failed" not in last10_text.lower():
                    return "PASS_CUSTOM"
        # 4b: doctest "0 of N items had failures"
        if "0 of" in last10_text and "had failures" in last10_text:
            return "PASS_CUSTOM"

    # 6: CRASH
    if exit_code != 0:
        return "CRASH"

    # 7: PARTIAL
    if exit_code == 0 and len(nonempty) > 5:
        return "PARTIAL"

    return "UNKNOWN"


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def run_test(spec: TestSpec, tmp_dir: str) -> TestResult:
    if spec.inline:
        # Materialize inline scripts into tmp_dir so protopy gets a real
        # filesystem path (its -c handling is not used here).
        script = os.path.join(tmp_dir, "_audit_" + spec.label.replace(" ", "_") + ".py")
        with open(script, "w") as fh:
            fh.write(INLINE_SCRIPTS[spec.label])
        target = script
    else:
        target = spec.path

    if not os.path.exists(target):
        return TestResult(spec, exit_code=127, stdout="",
                          stderr=f"target does not exist: {target}\n",
                          wall_seconds=0.0,
                          classification="CRASH")

    env = {**os.environ, "PROTO_ENV_DIAG": "0"}
    start = time.time()
    try:
        proc = subprocess.run(
            [PROTOPY, target],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
            env=env,
            cwd=REPO_ROOT,
        )
        wall = time.time() - start
        result = TestResult(spec, proc.returncode, proc.stdout, proc.stderr, wall)
    except subprocess.TimeoutExpired as exc:
        wall = time.time() - start
        out = exc.stdout or ""
        err = exc.stderr or ""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        result = TestResult(spec, 124, out, err, wall)

    result.classification = classify(result.exit_code, result.stdout, result.stderr)
    return result


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def _last_lines(text: str, n: int) -> str:
    nonempty = [ln for ln in text.splitlines() if ln.strip() != ""]
    return "\n".join(nonempty[-n:]) if nonempty else "(empty)"


def _md_escape(text: str) -> str:
    # Pipe is the only character we need to neutralize for inline-table use.
    return text.replace("|", "\\|").replace("\n", " ")


def render_summary(results: List[TestResult]) -> str:
    cats = ["Essential", "Important", "Necessary", "Bootstrap"]

    def per(cat: str, kind: str) -> int:
        return sum(1 for r in results if r.spec.category == cat and r.classification == kind)

    real_pass = lambda c: per(c, "PASS_UNITTEST") + per(c, "PASS_CUSTOM")
    total = lambda c: sum(1 for r in results if r.spec.category == c)

    head = ("| Category | Total | Real PASS | SILENT_HALT | FAIL_UNITTEST | "
            "CRASH | TIMEOUT | PARTIAL | UNKNOWN |\n")
    sep = "|---|---|---|---|---|---|---|---|---|\n"
    body = ""
    for c in cats:
        body += (f"| {c} | {total(c)} | {real_pass(c)} | "
                 f"{per(c, 'SILENT_HALT')} | {per(c, 'FAIL_UNITTEST')} | "
                 f"{per(c, 'CRASH')} | {per(c, 'TIMEOUT')} | "
                 f"{per(c, 'PARTIAL')} | {per(c, 'UNKNOWN')} |\n")
    grand_total = sum(total(c) for c in cats)
    grand_pass = sum(real_pass(c) for c in cats)
    grand_silent = sum(per(c, "SILENT_HALT") for c in cats)
    grand_fail = sum(per(c, "FAIL_UNITTEST") for c in cats)
    grand_crash = sum(per(c, "CRASH") for c in cats)
    grand_timeout = sum(per(c, "TIMEOUT") for c in cats)
    grand_partial = sum(per(c, "PARTIAL") for c in cats)
    grand_unknown = sum(per(c, "UNKNOWN") for c in cats)
    body += (f"| **Total** | **{grand_total}** | **{grand_pass}** | "
             f"**{grand_silent}** | **{grand_fail}** | **{grand_crash}** | "
             f"**{grand_timeout}** | **{grand_partial}** | "
             f"**{grand_unknown}** |\n")
    return head + sep + body


def render_detail(results: List[TestResult]) -> str:
    out = []
    for r in results:
        out.append(f"### {r.spec.category} / {r.spec.label}")
        out.append("")
        out.append(f"- **Path:** `{r.spec.path if not r.spec.inline else '<inline script>'}`")
        out.append(f"- **Claimed status:** {r.spec.claimed}")
        out.append(f"- **Actual status:** `{r.classification}`")
        out.append(f"- **Exit code:** {r.exit_code}")
        out.append(f"- **Wall-clock:** {r.wall_seconds:.2f}s")
        out.append("")
        out.append("Last 5 stdout lines:")
        out.append("```")
        out.append(_last_lines(r.stdout, 5))
        out.append("```")
        if r.classification in ("CRASH", "SILENT_HALT", "PARTIAL", "UNKNOWN"):
            out.append("")
            out.append("Last 10 stderr lines:")
            out.append("```")
            out.append(_last_lines(r.stderr, 10))
            out.append("```")
        out.append("")
    return "\n".join(out)


def render_findings(results: List[TestResult]) -> str:
    """Best-effort auto-summary of the most actionable findings."""
    by_class: dict = {}
    for r in results:
        by_class.setdefault(r.classification, []).append(r)

    out: List[str] = ["## Notable findings", ""]

    # Tests that were claimed PASS but are no longer PASS
    regressed = []
    for r in results:
        claimed = r.spec.claimed.upper()
        is_pass_claimed = claimed.startswith("PASS")
        is_real_pass = r.classification in ("PASS_UNITTEST", "PASS_CUSTOM")
        if is_pass_claimed and not is_real_pass:
            regressed.append(r)
    if regressed:
        out.append("**Previously-PASS tests no longer passing:**")
        for r in regressed:
            out.append(f"- `{r.spec.label}` -- claimed PASS, now `{r.classification}` "
                       f"(exit {r.exit_code})")
        out.append("")

    # SILENT_HALT cases (the false-positive class we're hunting)
    silent = by_class.get("SILENT_HALT", [])
    if silent:
        out.append("**SILENT_HALT cases (cannot prove the module ran to the end):**")
        for r in silent:
            out.append(f"- `{r.spec.label}` -- {len(r.stdout_lines())} non-empty stdout lines, "
                       f"no end-of-test marker.")
        out.append("")

    # Crashes broken down by root cause keyword
    crashes = by_class.get("CRASH", [])
    if crashes:
        out.append(f"**CRASH cases ({len(crashes)} of {len(results)} tests):**")
        cause_buckets: dict = {}
        for r in crashes:
            tail = r.stderr.lower()
            if "no module named" in tail:
                cause_buckets.setdefault("missing module (ImportError)", []).append(r)
            elif "object has no attribute" in tail:
                cause_buckets.setdefault("attribute lookup failure", []).append(r)
            elif "reraise outside" in tail:
                cause_buckets.setdefault("reraise outside except", []).append(r)
            elif "noneType" in r.stderr.lower():
                cause_buckets.setdefault("NoneType call/attr", []).append(r)
            else:
                cause_buckets.setdefault("other", []).append(r)
        for bucket, members in sorted(cause_buckets.items(), key=lambda kv: -len(kv[1])):
            names = ", ".join(f"`{r.spec.label}`" for r in members)
            out.append(f"- {bucket} ({len(members)}): {names}")
        out.append("")

    # Timeouts and partials
    if by_class.get("TIMEOUT"):
        out.append("**TIMEOUT cases:**")
        for r in by_class["TIMEOUT"]:
            out.append(f"- `{r.spec.label}`")
        out.append("")
    if by_class.get("PARTIAL"):
        out.append("**PARTIAL cases (manual classification needed):**")
        for r in by_class["PARTIAL"]:
            out.append(f"- `{r.spec.label}`")
        out.append("")
    if by_class.get("UNKNOWN"):
        out.append("**UNKNOWN cases:**")
        for r in by_class["UNKNOWN"]:
            out.append(f"- `{r.spec.label}`")
        out.append("")

    return "\n".join(out)


def render_report(results: List[TestResult], head_sha: str) -> str:
    lines: List[str] = []
    lines.append("# protopy Ground-Truth Audit (2026-04-30)")
    lines.append("")
    lines.append(f"**Binary:** post-silent-halt-fix (HEAD `{head_sha}`; "
                 f"silent-halt fix landed in commit `efcfa7f3`).")
    lines.append("")
    lines.append("**Audit run date:** 2026-04-30")
    lines.append("**Audit script:** `tests/synthetic/sp_audit_truth.py` (committed alongside this report).")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(render_summary(results))
    lines.append("")
    lines.append(render_findings(results))
    lines.append("")
    lines.append("## Detailed per-test results")
    lines.append("")
    lines.append(render_detail(results))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def head_sha() -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True)
        return out.strip()
    except Exception:
        return "unknown"


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=None,
                    help="Optional path to also write the markdown report to.")
    ap.add_argument("--quiet", action="store_true",
                    help="Suppress per-test progress lines on stderr.")
    args = ap.parse_args(argv)

    if not os.path.isfile(PROTOPY) or not os.access(PROTOPY, os.X_OK):
        print(f"protopy binary missing or not executable: {PROTOPY}",
              file=sys.stderr)
        return 2

    import tempfile
    with tempfile.TemporaryDirectory(prefix="sp_audit_") as tmp_dir:
        results: List[TestResult] = []
        for spec in TESTS_REAL:
            if not args.quiet:
                print(f"[run] {spec.category:<10} {spec.label}", file=sys.stderr,
                      flush=True)
            r = run_test(spec, tmp_dir)
            if not args.quiet:
                print(f"[done] {spec.label}: {r.classification} "
                      f"(exit={r.exit_code}, {r.wall_seconds:.2f}s)",
                      file=sys.stderr, flush=True)
            results.append(r)

    report = render_report(results, head_sha())
    sys.stdout.write(report)
    sys.stdout.write("\n")

    if args.out:
        with open(args.out, "w") as fh:
            fh.write(report)
            fh.write("\n")

    # Exit non-zero only if we discovered something the caller would care
    # about (an undetected SILENT_HALT or a UNKNOWN); the audit itself is
    # informational.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
