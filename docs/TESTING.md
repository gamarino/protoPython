# protoPython Testing Guide

## Test Targets

- **test_foundation** (`build/test/library/test_foundation`): Unit tests for `PythonEnvironment`, built-in types, and dunder methods.
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

## Known Issue: Foundation Test Hang

**Symptom**: `test_foundation` and `test_regr` (and any test that constructs `PythonEnvironment`) may hang during initialization, typically when creating the list prototype. The hang occurs inside protoCore’s allocation/GC path.

**Root cause**: Suspected GC deadlock when `PythonEnvironment` allocates many objects (object, type, int, str, list prototypes, etc.). The protoCore GC thread waits for `parkedThreads >= runningThreads`, while the main thread may be blocked or not properly participating in the stop-the-world protocol.

**Workaround**:

1. **CTest timeout**: `test_foundation` has a 60s timeout so CTest fails instead of hanging indefinitely.
2. **Debug reproducers**: Use `test_minimal` and `test_protocore_minimal` for manual debugging:
   - `test_protocore_minimal`: Pure protoCore (ProtoSpace + ProtoContext). Should pass.
   - `test_minimal`: Full `PythonEnvironment`. Reproduces the hang.
3. **protopy with --dry-run**: CLI exit-code tests use `--dry-run` and avoid instantiating `PythonEnvironment`, so they pass.

**Tracking**: See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Phase 2 and [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md). A fix likely requires changes in protoCore’s GC/`allocCell` synchronization.

## Incremental Regression Tracking

Use `test/regression/run_and_report.py` to track compatibility:

```bash
PROTYPY_BIN=./build/src/runtime/protopy python test/regression/run_and_report.py --output results/latest.json
```

Results can be persisted to JSON for history. See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Section 3 (Compatibility & Testing).
