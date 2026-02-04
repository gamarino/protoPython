# protoPython Testing Guide

## Test Targets

- **test_foundation** (`build/test/library/test_foundation`): Unit tests for `PythonEnvironment`, built-in types, and dunder methods.
- **test_execution_engine** (`build/test/library/test_execution_engine`): CTest for bytecode opcodes (e.g. BINARY_LSHIFT, INPLACE_SUBTRACT, BINARY_AND, INPLACE_MULTIPLY; see Next 20 Steps v24, v25).
- **test_regr** (`build/test/regression/test_regr`): Regression harness tests (e.g. module resolution).
- **protopy_cli_***: CTest targets for protopy CLI (help, missing module, script success).
- **regrtest_protopy_script**: Runs `protopy` on `regrtest_runner.py`.

## Running Tests

```bash
cd build
ctest -R test_foundation
ctest -R protopy_cli
ctest -R test_regr
# Or run executables directly:
./test/library/test_foundation
./test/regression/test_regr
```

## Startup / GC Hang (Resolved with protoCore Fix)

### Symptom `test_foundation` and `test_regr` (and any test that constructs `PythonEnvironment`) may hang during initialization, typically when creating the list prototype. The hang occurs inside protoCore’s allocation/GC path.

### Root cause Suspected GC deadlock when `PythonEnvironment` allocates many objects (object, type, int, str, list prototypes, etc.). The protoCore GC thread waits for `parkedThreads >= runningThreads`, while the main thread may be blocked or not properly participating in the stop-the-world protocol.

### Reproducer steps

1. Build protoPython: `cd build && cmake .. && make`
2. Run minimal reproducer: `./test/library/test_minimal`
3. If it hangs, the issue is reproduced. Compare with `./test/regression/test_protocore_minimal` (no protoPython) which should exit quickly.

### Workarounds

1. **CTest timeout**: `test_foundation` has a 60s timeout so CTest fails instead of hanging indefinitely.
2. **Debug reproducers**: Use `test_minimal` and `test_protocore_minimal` for manual debugging:
   - `test_protocore_minimal`: Pure protoCore (ProtoSpace + ProtoContext). Should pass.
   - `test_minimal`: Full `PythonEnvironment`. Reproduces the hang.
3. **protopy with --dry-run**: CLI exit-code tests use `--dry-run` and avoid instantiating `PythonEnvironment`, so they pass.
4. **Future**: A `--no-gc` workaround would require protoCore support to disable the GC thread at ProtoSpace creation time.

### Resolution (fixed in protoCore) The GC deadlock was fixed in protoCore’s `getFreeCells`: trigger GC only when there are no free cells (so the caller parks), and park/wait for GC completion when out of cells. See protoCore commit `fix(gc): resolve getFreeCells deadlock with GC thread`, and [STARTUP_GC_ANALYSIS.md](STARTUP_GC_ANALYSIS.md) for a full analysis. **Verification:** With a fixed protoCore, `./build/test/library/test_minimal` and `./build/src/runtime/protopy --module abc` should complete in a few seconds.

### Intermittent performance-test hang Script-based benchmarks (e.g. `protopy --script benchmarks/str_concat_loop.py`) can intermittently hit a 60s timeout. See [PERFORMANCE_HANG_DIAGNOSIS.md](PERFORMANCE_HANG_DIAGNOSIS.md) for diagnosis and mitigations (`--timeout`, `--cpython-only`). The benchmark harness supports `--timeout SECS` to fail fast when a run hangs.

### Resolve order (FoundationTest.SysModule) `PythonEnvironment::resolve()` resolves names in this order: (1) type shortcuts (object, type, int, list, …), (2) module import via `getImportModule` (e.g. `sys`, `builtins`, `os`), (3) builtins. So names like `sys` always resolve to the real module object, not a builtins attribute (e.g. a method) that would cause a type mismatch when calling `getAttribute` (protoCore expects an object cell, not a method cell).

## Incremental Regression Tracking

Incremental success is tracked via `test/regression/run_and_report.py`. Results can be persisted to JSON for history and CI.

### Persisting results

- **`--output <path>`**: Write results to a JSON file at `<path>`.
- **`REGRTEST_RESULTS=<path>`**: Same effect; use instead of or in addition to `--output` (env var is applied when writing).

Example:

```bash
PROTYPY_BIN=./build/src/runtime/protopy python test/regression/run_and_report.py --output results/latest.json
```

Or with env var:

```bash
PROTYPY_BIN=./build/src/runtime/protopy REGRTEST_RESULTS=test/regression/results/latest.json python test/regression/run_and_report.py
```

### JSON output format

The persisted JSON contains:

- `passed`, `failed`, `total`: Counts.
- `compatibility_pct`: Percentage of tests passed.
- `timestamp`: ISO timestamp (UTC).
- `failed_tests`: List of failing test names.

If the output directory contains or is configured with `REGRTEST_HISTORY`, the same payload is appended to a `history.json` file (up to the last 100 runs).

### CTest

The test `regrtest_persistence` (when Python 3 is available) runs `run_and_report.py` with `--output` to `test/regression/results/ctest_regrtest.json` and verifies the file exists and has the expected keys. This exercises the persistence path in CI.

### Execution engine: STORE_SUBSCR

STORE_SUBSCR (subscript assignment) is implemented via the container’s `__setitem__` method. It is exercised when running real Python code that assigns to list or dict subscripts (e.g. `a[0] = x`, `d[k] = v`), since list and dict prototypes provide `__setitem__`. There is no dedicated CTest that builds minimal bytecode (BUILD_LIST/BUILD_MAP) and runs STORE_SUBSCR on the resulting objects, because that path would hit protoCore’s embedded-value conversion in the test harness; the behavior is covered by execution of normal scripts.

See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Section 3 (Compatibility & Testing).
