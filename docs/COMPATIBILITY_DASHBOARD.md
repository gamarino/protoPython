# Compatibility Dashboard

Track protoPython regression test pass rate over time.

## What This Tracks

The dashboard shows the regrtest (CPython regression test suite) pass rate over time. Use `test/regression/run_and_report.py` to run the suite against the `protopy` binary and persist results to JSON. The dashboard visualizes pass/fail counts, compatibility percentage, and trends.

## Usage

Set `PROTOPY_BIN` to the path of the built `protopy` executable. Then:

1. Run regression tests and persist results:
   ```bash
   PROTOPY_BIN=./build/protopy REGRTEST_RESULTS=test/regression/results/latest.json \
     python test/regression/run_and_report.py --output test/regression/results/latest.json
   ```

2. Append to history (optional):
   ```bash
   REGRTEST_HISTORY=test/regression/results/history.json PROTOPY_BIN=... \
     run_and_report.py --output results/latest.json
   ```

3. View dashboard:
   ```bash
   python test/regression/dashboard.py test/regression/results/history.json
   ```

## Output

- `passed`, `failed`, `total`, `compatibility_pct`
- Latest timestamp
- Delta since first run (when history exists)
