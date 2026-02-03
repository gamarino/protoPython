#!/usr/bin/env python3
"""
Compatibility dashboard: display history and pass % from regrtest results.
Usage: dashboard.py [results/history.json]
       Or set REGRTEST_HISTORY to the history file path.
Reads JSON array of {timestamp, passed, failed, compatibility_pct} and prints summary.
"""

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_HISTORY = os.path.join(SCRIPT_DIR, "results", "history.json")


def main():
    path = os.environ.get("REGRTEST_HISTORY", DEFAULT_HISTORY)
    if len(sys.argv) > 1:
        path = sys.argv[1]

    if not os.path.isfile(path):
        print(f"No history file: {path}", file=sys.stderr)
        print("Run: PROTOPY_BIN=./protopy REGRTEST_RESULTS=results/latest.json run_and_report.py --output results/latest.json", file=sys.stderr)
        sys.exit(1)

    with open(path) as f:
        data = json.load(f)

    if isinstance(data, list):
        entries = data
    elif isinstance(data, dict):
        entries = [data]
    else:
        print("Invalid format", file=sys.stderr)
        sys.exit(1)

    total = len(entries)
    latest = entries[-1] if entries else {}
    pct = latest.get("compatibility_pct", 0)
    passed = latest.get("passed", 0)
    failed = latest.get("failed", 0)

    print("=== protoPython Compatibility Dashboard ===")
    print(f"History entries: {total}")
    print(f"Latest: {latest.get('timestamp', 'N/A')}")
    print(f"Passed: {passed}, Failed: {failed}, Compatibility: {pct}%")
    if entries and len(entries) > 1:
        first_pct = entries[0].get("compatibility_pct", 0)
        delta = pct - first_pct
        print(f"Delta since first: {delta:+.1f}%")


if __name__ == "__main__":
    main()
