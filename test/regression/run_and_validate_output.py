#!/usr/bin/env python3
"""
Run run_and_report.py with --output to a known path, then validate the JSON
has expected keys. Used by CTest to exercise regrtest persistence.
Exit 0 if persistence works and JSON is valid; non-zero otherwise.
"""

import json
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results")
OUTPUT_PATH = os.path.join(RESULTS_DIR, "ctest_regrtest.json")
REQUIRED_KEYS = ("passed", "failed", "total")


def main():
    protopy_bin = os.environ.get("PROTOPY_BIN")
    if not protopy_bin or not os.path.isfile(protopy_bin) or not os.access(protopy_bin, os.X_OK):
        print("PROTOPY_BIN must point to the protopy executable", file=sys.stderr)
        sys.exit(2)

    os.makedirs(RESULTS_DIR, exist_ok=True)
    cmd = [
        sys.executable,
        os.path.join(SCRIPT_DIR, "run_and_report.py"),
        protopy_bin,
        "--output",
        OUTPUT_PATH,
    ]
    try:
        subprocess.run(cmd, cwd=SCRIPT_DIR, capture_output=True, timeout=60)
        # We do not require exit 0: run_and_report exits 1 when any test fails.
        # This test only verifies that --output was written and has expected keys.
    except Exception as e:
        print(f"Error running run_and_report: {e}", file=sys.stderr)
        sys.exit(1)

    if not os.path.isfile(OUTPUT_PATH):
        print(f"Output file not created: {OUTPUT_PATH}", file=sys.stderr)
        sys.exit(1)

    try:
        with open(OUTPUT_PATH) as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        print(f"Invalid or unreadable JSON: {e}", file=sys.stderr)
        sys.exit(1)

    for key in REQUIRED_KEYS:
        if key not in data:
            print(f"Missing key in JSON: {key}", file=sys.stderr)
            sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
