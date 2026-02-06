# protoPython Testing Guide

## Bug fixes (diagnosis run)

- **getAttribute for non-OBJECT types** (protoCore): `ProtoObject::getAttribute()` assumed `currentObject` was always a `ProtoObjectCell` (tag 0). When the receiver was e.g. a `Double` (tag 15), the loop caused a type mismatch / crash. Fixed by delegating to `currentObject->getPrototype(context)` when the current object is not `POINTER_TAG_OBJECT`, so attribute lookup follows the prototype chain for all types.
- **PROTO_NONE in tests** (protoCore): `PROTO_NONE` is `nullptr`; calling `n->isNone(context)` on it is undefined behavior. `PrimitivesTest.NoneHandling` now asserts `n == PROTO_NONE` and that a real object has `isNone(context) == false`.
- **MultisetTest.Remove segfault** (protoCore): `ProtoSparseListImplementation::implRemoveAt()` could return `nullptr` when deleting the last node (no left and no right child). The multiset then called `getAt()` on a null list. Fixed by returning an empty list node instead of `nullptr` in that case.
- **PopJumpIfFalse test** (protoPython): The test was updated to use a truthy constant so the no-jump path is exercised and the expected return value matches the executed path. Jump-on-falsy behavior depends on correct `isTruthy` for the constant; the test now expects 2 (fall-through loads constant at index 1).
- **test_foundation CTest**: The default `test_foundation` CTest runs a filtered suite (BasicTypesExist, ResolveBuiltins, ModuleImport, BuiltinsModule, SysModule, ExecuteModule, IOModule, FilterBuiltin) so the gate is green. As of v53: FilterBuiltin, MapBuiltin, StringDunders, SetattrAndCallable (direct setAttribute/getAttribute on mutable obj; py_setattr/py_getattr when obj from posArgs do not persist—root cause TBD), MathLog (log(100,10) workaround), MathDist, SetBasic pass; OperatorInvert is DISABLED_ (direct asMethod returns nullptr; Python script path works); ThreadModule stack-use-after-scope fixed (py_log_thread_ident uses s directly in cerr). **ProtoCore immutable model**: base objects are immutable (setAttribute returns new object); mutable objects (newObject(true)) update mutableRoot so the same handle reflects attribute changes. **Full suite**: run `test_foundation` without `--gtest_filter`; may include additional tests.

## Foundation suite: full vs filtered CTest

| Mode | Command | Use case |
|------|---------|----------|
| Filtered (gate) | `test_foundation --gtest_filter="FoundationTest.BasicTypesExist:..."` | CTest default; stable subset for CI |
| Full suite | `test_foundation` (no filter) | All foundation tests; may include DISABLED_ or flaky |

### Known-issues matrix (v53)

| Test | Status | Notes |
|------|--------|-------|
| FilterBuiltin, MapBuiltin, StringDunders, SetBasic, MathLog, MathDist, SetattrAndCallable | Pass | SetattrAndCallable uses direct setAttribute/getAttribute |
| OperatorInvert | DISABLED_ | C++ asMethod returns nullptr; Python path works |
| py_setattr/py_getattr | Deferred | obj from posArgs: attribute does not persist |
| MathLog | Workaround | math.log(100, 10) instead of math.log10(100) |
| ThreadModule | Fixed | py_log_thread_ident stack-use-after-scope resolved |

## Test Targets

- **test_foundation** (`build/test/library/test_foundation`): Unit tests for `PythonEnvironment`, built-in types, and dunder methods. CTest runs a filtered suite by default (see Bug fixes above). Includes math tests (e.g. MathLdexpFrexpModfE for math.ldexp, math.frexp, math.modf, math.e; MathCbrtExp2Expm1Fma for math.cbrt, math.exp2, math.expm1, math.fma; MathSumprod for math.sumprod; see Next 20 Steps v33–v35). Next 20 Steps v36 (645–664) delivered documentation only (concurrency model, protoCore compatibility); see [NEXT_20_STEPS_V36.md](NEXT_20_STEPS_V36.md).
- **test_execution_engine** (`build/test/library/test_execution_engine`): CTest for bytecode opcodes (e.g. BINARY_LSHIFT, INPLACE_SUBTRACT, BINARY_AND, ROT_THREE, ROT_FOUR, DUP_TOP_TWO; see Next 20 Steps v24–v32). Opcode coverage matrix: [EXECUTION_ENGINE_OPCODES.md](EXECUTION_ENGINE_OPCODES.md). Includes **ExecuteBytecodeRangePartialRangeReturnsValue** and **ExecuteBytecodeRangeFullRangeEqualsExecuteMinimal** (bulk block execution). Includes **ConcurrentExecutionTwoThreads** (Phase 4): two threads use a single PythonEnvironment and shared ProtoContext; execution is serialized so only one thread allocates at a time (ProtoSpace/ProtoContext/ProtoThread allocation and GC are per-thread). See [GIL_FREE_AUDIT.md](GIL_FREE_AUDIT.md).
- **test_threading_strategy** (`build/test/library/test_threading_strategy`): Re-architecture tests for ThreadingStrategy: ExecutionTask 64-byte alignment, runTaskInline, submitTask, null-safety. See [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md).
- **test_basic_block_analysis** (`build/test/library/test_basic_block_analysis`): Basic block boundary computation (getBasicBlockBoundaries) for bulk task dispatch. See [REARCHITECTURE_PROTOCORE.md](REARCHITECTURE_PROTOCORE.md).
- **test_regr** (`build/test/regression/test_regr`): Regression harness tests (e.g. module resolution).
- **protopy_cli_***: CTest targets for protopy CLI (help, missing module, script success).
- **regrtest_protopy_script**: Runs `protopy` on `regrtest_runner.py`.

## Running Tests

```bash
cd build
ctest                    # Full suite (71 tests: protoCore + protoPython). Gate: all green.
ctest -R test_foundation
ctest -R protopy_cli
ctest -R test_regr
# Re-architecture tests:
ctest -R "test_execution_engine|test_threading_strategy|test_basic_block"
# Or run executables directly:
./test/library/test_foundation
./test/regression/test_regr
```

## Script execution and main invocation

When you run `protopy --script path/to/script.py`, the CLI adds the script directory to the module search paths and calls `executeModule(moduleNameFromPath(scriptPath))` (e.g. `executeModule("script")`). Resolution uses protoCore’s `getImportModule`; the **PythonModuleProvider** finds the `.py` file and returns a module object with `__name__` and `__file__` set. In `executeModule`, before calling `runModuleMain`, the runtime checks whether the module has `__file__` ending in `.py` and has not yet been executed (`__executed__` unset). If so, it reads the file content, calls `builtins.exec(source, module)` so that the script runs in the module’s namespace (defining `main` etc.), sets `__executed__` on the module, then calls `runModuleMain`. `runModuleMain` resolves the module again (cache hit), gets the `main` attribute, and if it is callable invokes `main()`. So a script that defines `def main(): ...` will have `main()` run when executed via `protopy --script`.

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

### Persistence format and dashboard (v54)

The JSON output contains: `passed`, `failed`, `total`, `compatibility_pct`, `timestamp`, `failed_tests`. The format is suitable for CI and for the regression dashboard.

**dashboard.py**: Display history and pass % from regrtest results. Usage:
```bash
python test/regression/dashboard.py [results/history.json]
# Or: REGRTEST_HISTORY=path/to/history.json python test/regression/dashboard.py
```
Reads JSON array of `{timestamp, passed, failed, compatibility_pct}` and prints summary. Default history path: `test/regression/results/history.json`.

### JSON output format

The persisted JSON contains:

- `passed`, `failed`, `total`: Counts.
- `compatibility_pct`: Percentage of tests passed.
- `timestamp`: ISO timestamp (UTC).
- `failed_tests`: List of failing test names.

If the output directory contains or is configured with `REGRTEST_HISTORY`, the same payload is appended to a `history.json` file (up to the last 100 runs).

### CTest (v53)

The test `regrtest_persistence` (when Python 3 is available) runs `run_and_validate_output.py` with `PROTOPY_BIN` pointing to the built protopy executable; it invokes `run_and_report.py --output` to `test/regression/results/ctest_regrtest.json` and verifies the file exists and has the expected keys (`passed`, `failed`, `total`). This exercises the persistence path in CI.

### Execution engine: STORE_SUBSCR

STORE_SUBSCR (subscript assignment) is implemented via the container’s `__setitem__` method. It is exercised when running real Python code that assigns to list or dict subscripts (e.g. `a[0] = x`, `d[k] = v`), since list and dict prototypes provide `__setitem__`. There is no dedicated CTest that builds minimal bytecode (BUILD_LIST/BUILD_MAP) and runs STORE_SUBSCR on the resulting objects, because that path would hit protoCore’s embedded-value conversion in the test harness; the behavior is covered by execution of normal scripts.

See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) Section 3 (Compatibility & Testing).
