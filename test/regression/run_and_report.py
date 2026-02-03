#!/usr/bin/env python3
"""
Run regression tests via protopy and report pass/fail for incremental success tracking.
Usage: run_and_report.py <protopy_binary> [test_script_or_module ...]
       run_and_report.py --output <path> [same as above]
       Or set PROTOPY_BIN and/or REGRTEST_RESULTS (output path).
Default: runs regrtest_runner.py (next to this script) if no tests given.
Exit code: 0 if all passed, 1 otherwise.
"""

import json
import os
import subprocess
import sys
from datetime import datetime, timezone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RESULTS_DIR = os.path.join(SCRIPT_DIR, "results")
DEFAULT_TESTS = [os.path.join(SCRIPT_DIR, "regrtest_runner.py")]


def main():
    protopy_bin = os.environ.get("PROTOPY_BIN")
    output_path = os.environ.get("REGRTEST_RESULTS")
    args = list(sys.argv[1:])

    if "--output" in args:
        i = args.index("--output")
        if i + 1 < len(args):
            output_path = args[i + 1]
            args = args[:i] + args[i + 2:]
        else:
            print("Error: --output requires a path", file=sys.stderr)
            sys.exit(2)

    if not protopy_bin and args and not args[0].startswith("-"):
        protopy_bin = args[0]
        args = args[1:]
    elif protopy_bin and args and args[0].startswith("--"):
        pass
    elif protopy_bin:
        args = args

    if not protopy_bin:
        print("Usage: run_and_report.py <protopy_binary> [test_script_or_module ...] [--output <path>]", file=sys.stderr)
        print("   or: PROTOPY_BIN=/path/to/protopy run_and_report.py [test ...] [--output <path>]", file=sys.stderr)
        print("   or: REGRTEST_RESULTS=<path> to persist results to JSON", file=sys.stderr)
        sys.exit(2)

    tests = args if args else DEFAULT_TESTS
    if not os.path.isfile(protopy_bin) or not os.access(protopy_bin, os.X_OK):
        print(f"Error: protopy binary not found or not executable: {protopy_bin}", file=sys.stderr)
        sys.exit(2)

    passed = 0
    failed = 0
    failed_names = []
    for t in tests:
        try:
            r = subprocess.run(
                [protopy_bin, t],
                capture_output=True,
                text=True,
                timeout=30,
                cwd=SCRIPT_DIR,
            )
            if r.returncode == 0:
                passed += 1
            else:
                failed += 1
                failed_names.append(t)
                print(f"FAIL {t}: exit {r.returncode}", file=sys.stderr)
                if r.stderr:
                    print(r.stderr, file=sys.stderr)
        except Exception as e:
            failed += 1
            failed_names.append(t)
            print(f"FAIL {t}: {e}", file=sys.stderr)

    total = passed + failed
    pct = (100.0 * passed / total) if total else 0
    print(f"Passed: {passed}, Failed: {failed}, Compatibility: {pct:.1f}%")

    if output_path:
        os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
        payload = {
            "passed": passed,
            "failed": failed,
            "total": total,
            "compatibility_pct": round(pct, 2),
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "failed_tests": failed_names,
        }
        with open(output_path, "w") as f:
            json.dump(payload, f, indent=2)
        print(f"Results written to {output_path}")

        history_path = os.environ.get("REGRTEST_HISTORY")
        if not history_path and os.path.dirname(output_path):
            history_path = os.path.join(os.path.dirname(output_path), "history.json")
        if history_path:
            history = []
            if os.path.isfile(history_path):
                with open(history_path) as hf:
                    history = json.load(hf)
            if not isinstance(history, list):
                history = [history] if history else []
            history.append(payload)
            with open(history_path, "w") as hf:
                json.dump(history[-100:], hf, indent=2)
            print(f"History appended to {history_path}")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
