# Compatibility Dashboard

Track protoPython regression test pass rate over time.

## Usage

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
