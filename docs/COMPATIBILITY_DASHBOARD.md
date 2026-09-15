# Compatibility Dashboard

Tracks the pass rate of CPython regression tests run under protoPython over time.

## What it tracks

`test/regression/run_and_report.py` runs CPython regression tests against a `protopy`
binary and writes the results to JSON; `test/regression/dashboard.py` summarises a
history file: pass and fail counts, compatibility percentage and trend.

## Usage

`run_and_report.py` takes the `protopy` binary as its first argument, or from the
`PROTOPY_BIN` environment variable when the argument is omitted. The output file is
given with `--output <path>` or with `REGRTEST_RESULTS`.

1. Run the regression tests and save the results:
   ```bash
   PROTOPY_BIN=build_release/src/runtime/protopy \
     python3 test/regression/run_and_report.py --output test/regression/results/latest.json
   ```

2. Also append the run to a history file (optional):
   ```bash
   REGRTEST_HISTORY=test/regression/results/history.json \
   PROTOPY_BIN=build_release/src/runtime/protopy \
     python3 test/regression/run_and_report.py --output test/regression/results/latest.json
   ```

3. Show the dashboard (the history path can also come from `REGRTEST_HISTORY`):
   ```bash
   python3 test/regression/dashboard.py test/regression/results/history.json
   ```

## Output

- `passed`, `failed`, `total`, `compatibility_pct`
- Timestamp of the latest run
- Change since the first run (when a history exists)
